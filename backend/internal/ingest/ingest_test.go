package ingest

import (
	"context"
	"errors"
	"net"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
	"github.com/carlosdrojas/fleetbus/backend/internal/metrics"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type fakeSink struct {
	mu      sync.Mutex
	batches []store.Batch
	block   chan struct{} // when non-nil WriteBatch waits for it to close
	fail    atomic.Int32  // number of upcoming calls to fail
}

func (f *fakeSink) WriteBatch(ctx context.Context, b store.Batch) error {
	if f.block != nil {
		select {
		case <-f.block:
		case <-ctx.Done():
			return ctx.Err()
		}
	}
	if f.fail.Add(-1) >= 0 {
		return errors.New("injected db failure")
	}
	f.fail.Store(0)
	f.mu.Lock()
	f.batches = append(f.batches, b)
	f.mu.Unlock()
	return nil
}
func (f *fakeSink) frames() (n int) {
	f.mu.Lock()
	defer f.mu.Unlock()
	for _, b := range f.batches {
		n += len(b.Telemetry)
	}
	return
}

type rig struct {
	srv  *Server
	sink *fakeSink
	conn *net.UDPConn
	m    *Metrics
	stop context.CancelFunc
}

func newRig(t *testing.T, sink *fakeSink, tweak func(*Config)) *rig {
	t.Helper()
	cfg := Config{Addr: "127.0.0.1:0", BatchWait: 5 * time.Millisecond, AckInterval: 5 * time.Millisecond, NackDamp: 30 * time.Millisecond, Logf: func(string, ...any) {}}
	if tweak != nil {
		tweak(&cfg)
	}
	m := NewMetrics(metrics.New())
	s := New(cfg, sink, m)
	if err := s.Listen(); err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	go s.Run(ctx)
	c, err := net.DialUDP("udp", nil, s.LocalAddr().(*net.UDPAddr))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { cancel(); c.Close() })
	return &rig{s, sink, c, m, cancel}
}

func (r *rig) send(t *testing.T, ptype uint8, seq uint32, payload []byte, flags uint8) {
	t.Helper()
	w, err := fbp.Encode(fbp.Packet{Type: ptype, Src: 7, Seq: seq, Payload: payload, Flags: flags})
	if err != nil {
		t.Fatal(err)
	}
	r.conn.Write(w)
}

func (r *rig) hbPkt(t *testing.T, seq uint32) { // gateway heartbeat: smallest valid payload
	r.send(t, fbp.TypeHeartbeat, seq, make([]byte, 14), 0)
}

// recv returns the next ACK/NACK from the server, or nil.
func (r *rig) recv(d time.Duration) *fbp.Packet {
	buf := make([]byte, 2048)
	r.conn.SetReadDeadline(time.Now().Add(d))
	n, err := r.conn.Read(buf)
	if err != nil {
		return nil
	}
	p, err := fbp.Decode(buf[:n])
	if err != nil {
		return nil
	}
	p.Payload = append([]byte(nil), p.Payload...)
	return &p
}

func (r *rig) lastAck(d time.Duration) uint32 {
	var cum uint32
	end := time.Now().Add(d)
	for time.Now().Before(end) {
		p := r.recv(20 * time.Millisecond)
		if p != nil && p.Type == fbp.TypeAck {
			cum, _ = fbp.DecodeAck(p.Payload)
		}
	}
	return cum
}

func eventually(t *testing.T, what string, cond func() bool) {
	t.Helper()
	for i := 0; i < 400; i++ {
		if cond() {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("timeout waiting for %s", what)
}

func TestAckFollowsCommit(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	for s := uint32(1); s <= 20; s++ {
		r.hbPkt(t, s)
	}
	if got := r.lastAck(300 * time.Millisecond); got != 20 {
		t.Fatalf("ack = %d, want 20", got)
	}
}

func TestNoAckWhileDatabaseIsBlocked(t *testing.T) {
	sink := &fakeSink{block: make(chan struct{})}
	r := newRig(t, sink, nil)
	for s := uint32(1); s <= 10; s++ {
		r.hbPkt(t, s)
	}
	if got := r.lastAck(200 * time.Millisecond); got != 0 {
		t.Fatalf("acked %d packets that are not in the database", got)
	}
	close(sink.block)
	if got := r.lastAck(300 * time.Millisecond); got != 10 {
		t.Fatalf("after unblock ack = %d, want 10", got)
	}
}

func TestDatabaseFailureIsRetriedWithoutLossOrDuplication(t *testing.T) {
	sink := &fakeSink{}
	sink.fail.Store(2)
	r := newRig(t, sink, nil)
	oldSleep := 0
	_ = oldSleep
	for s := uint32(1); s <= 5; s++ {
		r.hbPkt(t, s)
	}
	if got := r.lastAck(1800 * time.Millisecond); got != 5 {
		t.Fatalf("ack = %d, want 5 (after retries)", got)
	}
	sink.mu.Lock()
	n := 0
	for _, b := range sink.batches {
		n += len(b.Gateways)
	}
	sink.mu.Unlock()
	if n < 1 || r.m.DBErrors.Value() != 2 {
		t.Fatalf("batches=%d db_errors=%d", n, r.m.DBErrors.Value())
	}
}

func TestReorderedPacketsAreDeliveredOnceAndGapIsNacked(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	r.hbPkt(t, 2)
	r.hbPkt(t, 3)
	var nack []uint32
	for i := 0; i < 20 && nack == nil; i++ {
		if p := r.recv(20 * time.Millisecond); p != nil && p.Type == fbp.TypeNack {
			nack, _ = fbp.DecodeNack(p.Payload)
		}
	}
	if len(nack) != 1 || nack[0] != 1 {
		t.Fatalf("nack = %v, want [1]", nack)
	}
	r.hbPkt(t, 1)
	r.hbPkt(t, 2) // duplicate
	if got := r.lastAck(300 * time.Millisecond); got != 3 {
		t.Fatalf("ack = %d, want 3", got)
	}
	if r.m.Dups.Value() != 1 {
		t.Fatalf("dups = %d", r.m.Dups.Value())
	}
	eventually(t, "3 items committed", func() bool { return r.m.Items.Value() == 3 })
}

func TestNackIsRepeatedButDamped(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	r.hbPkt(t, 2)
	count := 0
	end := time.Now().Add(250 * time.Millisecond)
	for time.Now().Before(end) {
		if p := r.recv(10 * time.Millisecond); p != nil && p.Type == fbp.TypeNack {
			count++
		}
	}
	if count < 2 || count > 10 { // 250 ms at a 30 ms damp, not one per 5 ms tick (50)
		t.Fatalf("nacks = %d", count)
	}
}

func TestCorruptDatagramsAreCountedAndIgnored(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	good, _ := fbp.Encode(fbp.Packet{Type: fbp.TypeHeartbeat, Src: 7, Seq: 1, Payload: make([]byte, 14)})
	bad := append([]byte(nil), good...)
	bad[len(bad)-1] ^= 0xFF
	r.conn.Write(bad)
	r.conn.Write([]byte{1, 2, 3})
	r.conn.Write(good)
	if got := r.lastAck(200 * time.Millisecond); got != 1 {
		t.Fatalf("ack = %d", got)
	}
	if r.m.BadDatagrams.Value() != 2 {
		t.Fatalf("bad = %d", r.m.BadDatagrams.Value())
	}
}

func TestGatewayThatLostItsSpoolStartsANewSession(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	for s := uint32(1); s <= 100; s++ {
		r.hbPkt(t, s)
	}
	if got := r.lastAck(300 * time.Millisecond); got != 100 {
		t.Fatalf("ack = %d", got)
	}
	r.hbPkt(t, 1) // fresh gateway install, numbering restarts
	r.hbPkt(t, 2)
	eventually(t, "new session committed", func() bool { return r.m.Items.Value() == 102 })
	if got := r.lastAck(300 * time.Millisecond); got != 2 {
		t.Fatalf("ack after restart = %d, want 2", got)
	}
}

func TestTelemetryIsDecodedIntoMetricsHeartbeatsAndFaults(t *testing.T) {
	sink := &fakeSink{}
	r := newRig(t, sink, nil)
	hb := []byte{2, 1, 0, 0x10, 0x27, 0, 0x11 | 0x20 | 0x10 - 0x10, 1} // body, fw=1, uptime 10000 s, state NORMAL, slot 1? set below
	hb[6] = 1 | 1<<4 | 1<<5                                            // state NORMAL, slot 1, trial
	bat := []byte{0xD8, 0x0E, 0x00, 0x00, 0xCE, 0xFF, 66, 1}           // 3800 mV, 0 mA, -5.0 C, 66 %
	flt := []byte{0x10, 0x00, 2, 1, 0, 0, 0, 1}
	tele, _ := fbp.EncodeTelemetry(fbp.Telemetry{BaseTsMs: uint64(time.Now().UnixMilli()), Records: []fbp.CanRecord{
		{CanID: 0x100, Data: hb}, {DtMs: 10, CanID: 0x200, Data: bat}, {DtMs: 20, CanID: 0x300, Data: flt}, {DtMs: 30, CanID: 0x1AB, Data: []byte{9}}}})
	r.send(t, fbp.TypeTelemetry, 1, tele, fbp.FlagSimulated)
	eventually(t, "batch", func() bool { return sink.frames() == 4 })
	b := sink.batches[0]
	if len(b.Heartbeats) != 1 || b.Heartbeats[0].NodeID != 2 || b.Heartbeats[0].Slot != 1 || !b.Heartbeats[0].Trial || b.Heartbeats[0].State != 1 {
		t.Fatalf("heartbeats: %+v", b.Heartbeats)
	}
	if len(b.Metrics) != 4 || b.Metrics[2].Name != "temp_c" || b.Metrics[2].Value != -5 {
		t.Fatalf("metrics: %+v", b.Metrics)
	}
	if len(b.Faults) != 1 || b.Faults[0].Code != 0x10 || b.Faults[0].NodeID != 1 {
		t.Fatalf("faults: %+v", b.Faults)
	}
	if !b.Gateways[0].Simulated || b.Gateways[0].ID != 7 {
		t.Fatalf("gateway: %+v", b.Gateways)
	}
}

func TestMalformedPayloadDoesNotWedgeThePipeline(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	r.send(t, fbp.TypeTelemetry, 1, []byte{1, 2, 3}, 0) // valid CRC, garbage payload
	r.hbPkt(t, 2)
	if got := r.lastAck(300 * time.Millisecond); got != 2 {
		t.Fatalf("ack = %d", got)
	}
	if r.m.BadPayload.Value() != 1 {
		t.Fatalf("bad payload = %d", r.m.BadPayload.Value())
	}
}

func TestSendToGatewayUsesLearnedAddress(t *testing.T) {
	r := newRig(t, &fakeSink{}, nil)
	if err := r.srv.Send(7, fbp.TypeOtaOffer, make([]byte, 14)); err == nil {
		t.Fatal("must fail before the gateway is known")
	}
	r.hbPkt(t, 1)
	eventually(t, "peer known", func() bool { return r.srv.Send(7, fbp.TypeOtaOffer, make([]byte, 14)) == nil })
	for i := 0; i < 20; i++ {
		if p := r.recv(20 * time.Millisecond); p != nil && p.Type == fbp.TypeOtaOffer {
			return
		}
	}
	t.Fatal("offer not received")
}
