// Package ingest is the UDP front door: it decodes FBP datagrams, de-duplicates and reorders per gateway, pushes
// delivered packets through a bounded queue to a batch writer, and ACKs only what has been COMMITTED to the database.
// Backpressure therefore never loses data: when the queue or database stalls, ACKs stop, datagrams are dropped by the
// kernel or ignored, and the gateway keeps them in its disk spool and retransmits.
package ingest

import (
	"context"
	"errors"
	"log"
	"net"
	"sync"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
	"github.com/carlosdrojas/fleetbus/backend/internal/metrics"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type Sink interface {
	WriteBatch(ctx context.Context, b store.Batch) error
}

type Config struct {
	Addr        string
	QueueSize   int           // bounded queue between the UDP loop and the writer
	BatchMax    int           // items per transaction
	BatchWait   time.Duration // max time to wait for a batch to fill
	Window      uint32        // reorder window per gateway
	AckInterval time.Duration
	NackDamp    time.Duration
	Logf        func(string, ...any)
}

func (c *Config) defaults() {
	if c.QueueSize == 0 {
		c.QueueSize = 4096
	}
	if c.BatchMax == 0 {
		c.BatchMax = 500
	}
	if c.BatchWait == 0 {
		c.BatchWait = 50 * time.Millisecond
	}
	if c.Window == 0 {
		c.Window = 256
	}
	if c.AckInterval == 0 {
		c.AckInterval = 25 * time.Millisecond
	}
	if c.NackDamp == 0 {
		c.NackDamp = 100 * time.Millisecond
	}
	if c.Logf == nil {
		c.Logf = log.Printf
	}
}

type Metrics struct {
	Datagrams, BadDatagrams, BadPayload, BadFrames, Dups, TooFar, Retransmits *metrics.Counter
	QueueFullWaits, Items, Batches, DBErrors, Acks, Nacks, Frames             *metrics.Counter
	QueueDepth, Gateways                                                      *metrics.Gauge
	WriteNanos                                                                *metrics.Counter
}

func NewMetrics(r *metrics.Registry) *Metrics {
	return &Metrics{
		Datagrams:      r.Counter("fleetbus_ingest_datagrams_total", "UDP datagrams received"),
		BadDatagrams:   r.Counter("fleetbus_ingest_bad_datagrams_total", "datagrams rejected by the FBP decoder (CRC, magic, length, ...)"),
		BadPayload:     r.Counter("fleetbus_ingest_bad_payload_total", "packets with a valid CRC but a malformed payload"),
		BadFrames:      r.Counter("fleetbus_ingest_bad_can_frames_total", "known CAN ids with a bad layout or protocol version"),
		Dups:           r.Counter("fleetbus_ingest_duplicates_total", "duplicate packets dropped"),
		TooFar:         r.Counter("fleetbus_ingest_outside_window_total", "packets too far ahead of the reorder window"),
		Retransmits:    r.Counter("fleetbus_ingest_retransmits_seen_total", "packets arriving with the retransmit flag"),
		QueueFullWaits: r.Counter("fleetbus_ingest_queue_full_waits_total", "times the UDP loop had to wait because the queue was full (backpressure)"),
		Items:          r.Counter("fleetbus_ingest_items_committed_total", "FBP packets committed to the database"),
		Frames:         r.Counter("fleetbus_ingest_can_frames_committed_total", "CAN frames committed to the database"),
		Batches:        r.Counter("fleetbus_ingest_batches_total", "database transactions"),
		DBErrors:       r.Counter("fleetbus_ingest_db_errors_total", "failed batch writes (retried)"),
		Acks:           r.Counter("fleetbus_ingest_acks_sent_total", "ACK packets sent"),
		Nacks:          r.Counter("fleetbus_ingest_nacks_sent_total", "NACK packets sent"),
		WriteNanos:     r.Counter("fleetbus_ingest_db_write_nanoseconds_total", "wall time spent in WriteBatch"),
		QueueDepth:     r.Gauge("fleetbus_ingest_queue_depth", "items waiting for the writer"),
		Gateways:       r.Gauge("fleetbus_ingest_gateways", "gateways seen since start"),
	}
}

type peer struct {
	mu        sync.Mutex
	src       uint8
	addr      *net.UDPAddr
	sim       bool
	re        *fbp.Reorderer
	committed uint32
	lastAcked uint32
	dirty     bool
	nacked    map[uint32]time.Time
	outSeq    uint32
	epoch     uint32 // bumped when the gateway starts a new session (spool lost, seq restarts at 1)
}

type Server struct {
	cfg   Config
	sink  Sink
	m     *Metrics
	conn  *net.UDPConn
	queue chan item

	mu    sync.Mutex
	peers map[uint8]*peer
}

func New(cfg Config, sink Sink, m *Metrics) *Server {
	cfg.defaults()
	return &Server{cfg: cfg, sink: sink, m: m, queue: make(chan item, cfg.QueueSize), peers: map[uint8]*peer{}}
}

func (s *Server) LocalAddr() net.Addr { return s.conn.LocalAddr() }

// Listen binds the socket (separate from Run so callers can learn the port).
func (s *Server) Listen() error {
	a, err := net.ResolveUDPAddr("udp", s.cfg.Addr)
	if err != nil {
		return err
	}
	s.conn, err = net.ListenUDP("udp", a)
	if err != nil {
		return err
	}
	s.conn.SetReadBuffer(8 << 20)
	return nil
}

func (s *Server) Run(ctx context.Context) error {
	if s.conn == nil {
		if err := s.Listen(); err != nil {
			return err
		}
	}
	var wg sync.WaitGroup
	wg.Add(2)
	go func() { defer wg.Done(); s.writer(ctx) }()
	go func() { defer wg.Done(); s.acker(ctx) }()
	go func() { <-ctx.Done(); s.conn.Close() }()
	err := s.reader(ctx)
	wg.Wait()
	if ctx.Err() != nil {
		return nil
	}
	return err
}

func (s *Server) getPeer(src uint8) *peer {
	s.mu.Lock()
	defer s.mu.Unlock()
	p, ok := s.peers[src]
	if !ok {
		p = &peer{src: src, re: fbp.NewReorderer(s.cfg.Window), nacked: map[uint32]time.Time{}}
		s.peers[src] = p
		s.m.Gateways.Set(int64(len(s.peers)))
	}
	return p
}

func (s *Server) reader(ctx context.Context) error {
	buf := make([]byte, 2048)
	for {
		n, addr, err := s.conn.ReadFromUDP(buf)
		if err != nil {
			if ctx.Err() != nil || errors.Is(err, net.ErrClosed) {
				return nil
			}
			return err
		}
		s.m.Datagrams.Inc()
		pkt, err := fbp.Decode(buf[:n])
		if err != nil {
			s.m.BadDatagrams.Inc()
			continue
		}
		if pkt.Flags&fbp.FlagRetransmit != 0 {
			s.m.Retransmits.Inc()
		}
		switch pkt.Type {
		case fbp.TypeTelemetry, fbp.TypeHeartbeat, fbp.TypeOtaStatus:
		default:
			continue // ACK/NACK/OTA_OFFER/OTA_CHUNK flow the other way
		}
		p := s.getPeer(pkt.Src)
		p.mu.Lock()
		if pkt.Seq == 1 && pkt.Flags&fbp.FlagRetransmit == 0 && p.re.CumAck() > 64 {
			// A first-send seq 1 after we already acked >64 packets can only be a gateway that lost its spool. Start over.
			s.cfg.Logf("ingest: gateway %d restarted its sequence (new session)", pkt.Src)
			p.re, p.committed, p.lastAcked, p.epoch = fbp.NewReorderer(s.cfg.Window), 0, 0, p.epoch+1
			p.nacked = map[uint32]time.Time{}
		}
		p.addr, p.sim, p.dirty = addr, pkt.Flags&fbp.FlagSimulated != 0, true
		dupsBefore, farBefore := p.re.Dups, p.re.TooFar
		delivered := p.re.Push(pkt)
		s.m.Dups.Add(int64(p.re.Dups - dupsBefore))
		s.m.TooFar.Add(int64(p.re.TooFar - farBefore))
		recv := time.Now()
		items := make([]item, len(delivered))
		for i, d := range delivered {
			items[i] = item{src: d.Src, seq: d.Seq, epoch: p.epoch, ptype: d.Type, sim: p.sim, addr: addr.String(), recv: recv, payload: append([]byte(nil), d.Payload...)}
		}
		p.mu.Unlock()
		for _, it := range items {
			select {
			case s.queue <- it:
			default:
				s.m.QueueFullWaits.Inc()
				select {
				case s.queue <- it:
				case <-ctx.Done():
					return nil
				}
			}
		}
		s.m.QueueDepth.Set(int64(len(s.queue)))
	}
}

func (s *Server) writer(ctx context.Context) {
	for {
		var batch []item
		select {
		case it := <-s.queue:
			batch = append(batch, it)
		case <-ctx.Done():
			return
		}
		timer := time.NewTimer(s.cfg.BatchWait)
	fill:
		for len(batch) < s.cfg.BatchMax {
			select {
			case it := <-s.queue:
				batch = append(batch, it)
			case <-timer.C:
				break fill
			case <-ctx.Done():
				timer.Stop()
				return
			}
		}
		timer.Stop()
		b := buildBatch(batch, s.m)
		for {
			t0 := time.Now()
			err := s.sink.WriteBatch(ctx, b)
			s.m.WriteNanos.Add(int64(time.Since(t0)))
			if err == nil {
				break
			}
			s.m.DBErrors.Inc()
			s.cfg.Logf("ingest: write failed (will retry, data is not acked): %v", err)
			select {
			case <-time.After(500 * time.Millisecond):
			case <-ctx.Done():
				return
			}
		}
		s.m.Batches.Inc()
		s.m.Items.Add(int64(len(batch)))
		s.m.Frames.Add(int64(len(b.Telemetry)))
		for _, it := range batch { // FIFO per gateway, so the last item seen carries the highest committed seq
			p := s.getPeer(it.src)
			p.mu.Lock()
			if it.epoch == p.epoch && it.seq > p.committed {
				p.committed = it.seq
			}
			p.mu.Unlock()
		}
		s.m.QueueDepth.Set(int64(len(s.queue)))
	}
}

func (s *Server) acker(ctx context.Context) {
	t := time.NewTicker(s.cfg.AckInterval)
	defer t.Stop()
	for {
		select {
		case <-t.C:
		case <-ctx.Done():
			return
		}
		s.mu.Lock()
		peers := make([]*peer, 0, len(s.peers))
		for _, p := range s.peers {
			peers = append(peers, p)
		}
		s.mu.Unlock()
		now := time.Now()
		for _, p := range peers {
			p.mu.Lock()
			cum := p.re.CumAck()
			if p.committed < cum {
				cum = p.committed // never acknowledge data that is not in the database yet
			}
			sendAck := (cum != p.lastAcked || p.dirty) && cum > 0
			var nack []uint32
			for _, seq := range p.re.Missing(64) {
				if last, ok := p.nacked[seq]; !ok || now.Sub(last) >= s.cfg.NackDamp {
					nack = append(nack, seq)
					p.nacked[seq] = now
				}
			}
			for seq := range p.nacked {
				if seq < p.re.CumAck() {
					delete(p.nacked, seq)
				}
			}
			p.lastAcked, p.dirty = cum, false
			addr := p.addr
			p.mu.Unlock()
			if addr == nil {
				continue
			}
			if sendAck {
				s.sendTo(p, fbp.TypeAck, fbp.EncodeAck(cum))
				s.m.Acks.Inc()
			}
			if len(nack) > 0 {
				pl, _ := fbp.EncodeNack(nack)
				s.sendTo(p, fbp.TypeNack, pl)
				s.m.Nacks.Inc()
			}
		}
	}
}

func (s *Server) sendTo(p *peer, ptype uint8, payload []byte) error {
	p.mu.Lock()
	p.outSeq++
	seq, addr := p.outSeq, p.addr
	p.mu.Unlock()
	if addr == nil {
		return errors.New("gateway address unknown")
	}
	w, err := fbp.Encode(fbp.Packet{Type: ptype, Src: 0, Seq: seq, Payload: payload})
	if err != nil {
		return err
	}
	_, err = s.conn.WriteToUDP(w, addr)
	return err
}

// Send implements ota.Sender: backend -> gateway control/OTA packets.
func (s *Server) Send(gateway int16, ptype uint8, payload []byte) error {
	s.mu.Lock()
	p := s.peers[uint8(gateway)]
	s.mu.Unlock()
	if p == nil {
		return errors.New("gateway not connected")
	}
	return s.sendTo(p, ptype, payload)
}
