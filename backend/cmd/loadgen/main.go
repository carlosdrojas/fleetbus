// loadgen: measures ingest throughput and checks for loss. N simulated gateways send reliable FBP telemetry
// (window, cumulative ACK, retransmit on timeout) at a target rate; at the end it reports achieved committed rate and,
// with -dsn, verifies the database holds exactly the frames that were acknowledged.
package main

import (
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"net"
	"os"
	"sync"
	"sync/atomic"
	"time"

	"github.com/jackc/pgx/v5"

	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
)

type result struct {
	sent, resent, acked uint64
	frames              uint64
}

func main() {
	target := flag.String("target", "127.0.0.1:9000", "ingest UDP address")
	gws := flag.Int("gateways", 4, "simulated gateways")
	rate := flag.Int("rate", 2000, "CAN frames per second per gateway (0 = as fast as the window allows)")
	frames := flag.Int("batch", 20, "CAN frames per FBP telemetry packet")
	dur := flag.Duration("duration", 10*time.Second, "send duration")
	dsn := flag.String("dsn", "", "Postgres DSN to verify stored rows (optional)")
	base := flag.Int("base-gw", 100, "first gateway id (use ids that real gateways do not)")
	flag.Parse()

	conn0, err := net.ResolveUDPAddr("udp", *target)
	if err != nil {
		fatal(err)
	}
	var wg sync.WaitGroup
	res := make([]result, *gws)
	start := time.Now()
	for i := 0; i < *gws; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			res[i] = runGateway(conn0, uint8(*base+i), *rate, *frames, *dur)
		}(i)
	}
	wg.Wait()
	el := time.Since(start)
	var tot result
	for _, r := range res {
		tot.sent += r.sent
		tot.resent += r.resent
		tot.acked += r.acked
		tot.frames += r.frames
	}
	fmt.Printf("gateways=%d duration=%s\n", *gws, el.Round(time.Millisecond))
	fmt.Printf("packets sent=%d resent=%d acked=%d (%.1f%% resend overhead)\n", tot.sent, tot.resent, tot.acked, 100*float64(tot.resent)/float64(max(tot.sent, 1)))
	fmt.Printf("CAN frames acknowledged (committed to Postgres): %d  =>  %.0f frames/s overall, %.0f packets/s\n",
		tot.frames, float64(tot.frames)/el.Seconds(), float64(tot.acked)/el.Seconds())
	if *dsn != "" {
		conn, err := pgx.Connect(context.Background(), *dsn)
		if err != nil {
			fatal(err)
		}
		var n int64
		conn.QueryRow(context.Background(), `SELECT count(*) FROM telemetry WHERE gateway_id >= $1 AND gateway_id < $2`, *base, *base+*gws).Scan(&n)
		verdict := "OK: database holds exactly the acknowledged frames"
		if uint64(n) != tot.frames {
			verdict = "MISMATCH"
		}
		fmt.Printf("telemetry rows for these gateways in Postgres: %d (acknowledged %d) -> %s\n", n, tot.frames, verdict)
		if uint64(n) != tot.frames {
			os.Exit(1)
		}
	}
}

func runGateway(addr *net.UDPAddr, id uint8, rate, perPacket int, dur time.Duration) result {
	c, err := net.DialUDP("udp", nil, addr)
	if err != nil {
		fatal(err)
	}
	defer c.Close()
	const window = 128
	type inflight struct {
		wire []byte
		sent time.Time
		n    int
	}
	var mu sync.Mutex
	pend := map[uint32]*inflight{}
	var cum uint32
	var r result
	var ackedFrames atomic.Uint64
	done := make(chan struct{})
	go func() { // ack reader
		buf := make([]byte, 2048)
		for {
			c.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
			n, err := c.Read(buf)
			select {
			case <-done:
				return
			default:
			}
			if err != nil {
				continue
			}
			p, err := fbp.Decode(buf[:n])
			if err != nil || p.Type != fbp.TypeAck {
				continue
			}
			a, _ := fbp.DecodeAck(p.Payload)
			mu.Lock()
			for s := cum + 1; s <= a; s++ {
				if f, ok := pend[s]; ok {
					ackedFrames.Add(uint64(f.n))
					delete(pend, s)
					r.acked++
				}
			}
			if a > cum {
				cum = a
			}
			mu.Unlock()
		}
	}()
	interval := time.Duration(0)
	if rate > 0 {
		interval = time.Duration(float64(time.Second) * float64(perPacket) / float64(rate))
	}
	end := time.Now().Add(dur)
	var seq uint32
	next := time.Now()
	for time.Now().Before(end) {
		mu.Lock()
		full := len(pend) >= window
		// retransmit anything unacked for a while
		now := time.Now()
		for _, f := range pend {
			if now.Sub(f.sent) > 300*time.Millisecond {
				w := append([]byte(nil), f.wire...)
				w[5] |= fbp.FlagRetransmit
				binary.LittleEndian.PutUint32(w[len(w)-4:], fbp.CRC32C(w[:len(w)-4]))
				c.Write(w)
				f.sent = now
				r.resent++
			}
		}
		mu.Unlock()
		if full {
			time.Sleep(time.Millisecond)
			continue
		}
		if interval > 0 {
			if d := time.Until(next); d > 0 {
				time.Sleep(d)
			}
			next = next.Add(interval)
		}
		seq++
		recs := make([]fbp.CanRecord, perPacket)
		for i := range recs {
			switch i % 3 { // heartbeat / battery / body, the real mix
			case 0:
				recs[i] = fbp.CanRecord{DtMs: uint16(i), CanID: 0x100, Data: []byte{byte(1 + i%2), 1, 0, 1, 0, 0, 1, 1}}
			case 1:
				recs[i] = fbp.CanRecord{DtMs: uint16(i), CanID: 0x200, Data: []byte{0xD8, 0x0E, 0xF4, 0x01, 0xFA, 0, 66, 1}}
			default:
				recs[i] = fbp.CanRecord{DtMs: uint16(i), CanID: 0x201, Data: []byte{1, 90, 0, 1}}
			}
		}
		pl, _ := fbp.EncodeTelemetry(fbp.Telemetry{BaseTsMs: uint64(time.Now().UnixMilli()), Records: recs})
		w, _ := fbp.Encode(fbp.Packet{Type: fbp.TypeTelemetry, Src: id, Seq: seq, Payload: pl, Flags: fbp.FlagSimulated})
		mu.Lock()
		pend[seq] = &inflight{wire: w, sent: time.Now(), n: perPacket}
		mu.Unlock()
		c.Write(w)
		r.sent++
	}
	// drain: wait for the outstanding window to be acknowledged (with retransmits)
	deadline := time.Now().Add(15 * time.Second)
	for time.Now().Before(deadline) {
		mu.Lock()
		left := len(pend)
		now := time.Now()
		for _, f := range pend {
			if now.Sub(f.sent) > 300*time.Millisecond {
				w := append([]byte(nil), f.wire...)
				w[5] |= fbp.FlagRetransmit
				binary.LittleEndian.PutUint32(w[len(w)-4:], fbp.CRC32C(w[:len(w)-4]))
				c.Write(w)
				f.sent = now
				r.resent++
			}
		}
		mu.Unlock()
		if left == 0 {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	close(done)
	r.frames = ackedFrames.Load()
	return r
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, "loadgen:", err)
	os.Exit(1)
}
