package fbp

// Reorderer delivers packets from one sender in sequence order, drops duplicates, and reports gaps.
// Window bounds memory: a packet more than Window ahead of the next expected seq is rejected (counted as TooFar).
// Sequence numbers start at 1; wraparound is not handled (2^32 packets at 1 kHz is ~49 days; the gateway
// restarts its numbering with a new session on reconnect, see spec).
type Reorderer struct {
	Window uint32
	next   uint32 // next seq to deliver
	held   map[uint32]Packet
	Dups   uint64
	TooFar uint64
}

func NewReorderer(window uint32) *Reorderer {
	return &Reorderer{Window: window, next: 1, held: map[uint32]Packet{}}
}

// CumAck is the highest seq such that all seqs <= it were delivered (0 if none).
func (r *Reorderer) CumAck() uint32 { return r.next - 1 }

// Push offers a packet; it returns the packets now deliverable, in order.
func (r *Reorderer) Push(p Packet) []Packet {
	if p.Seq < r.next {
		r.Dups++
		return nil
	}
	if p.Seq-r.next >= r.Window {
		r.TooFar++
		return nil
	}
	if _, dup := r.held[p.Seq]; dup {
		r.Dups++
		return nil
	}
	if p.Seq != r.next {
		cp := p
		cp.Payload = append([]byte(nil), p.Payload...) // the datagram buffer will be reused
		r.held[p.Seq] = cp
		return nil
	}
	out := []Packet{p}
	r.next++
	for {
		q, ok := r.held[r.next]
		if !ok {
			return out
		}
		delete(r.held, r.next)
		out = append(out, q)
		r.next++
	}
}

// Missing lists up to max seqs between the cumulative ack and the highest held packet that have not arrived.
func (r *Reorderer) Missing(max int) []uint32 {
	var hi uint32
	for s := range r.held {
		if s > hi {
			hi = s
		}
	}
	var out []uint32
	for s := r.next; s < hi && len(out) < max; s++ {
		if _, ok := r.held[s]; !ok {
			out = append(out, s)
		}
	}
	return out
}
