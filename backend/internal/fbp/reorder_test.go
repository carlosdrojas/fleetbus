package fbp

import (
	"math/rand"
	"testing"
)

func pk(s uint32) Packet { return Packet{Seq: s, Payload: []byte{byte(s)}} }

func seqs(ps []Packet) []uint32 {
	var o []uint32
	for _, p := range ps {
		o = append(o, p.Seq)
	}
	return o
}

func TestInOrder(t *testing.T) {
	r := NewReorderer(8)
	for s := uint32(1); s <= 5; s++ {
		if got := seqs(r.Push(pk(s))); len(got) != 1 || got[0] != s {
			t.Fatalf("seq %d: %v", s, got)
		}
	}
	if r.CumAck() != 5 {
		t.Fatal(r.CumAck())
	}
}

func TestGapFillsAndReleasesInOrder(t *testing.T) {
	r := NewReorderer(8)
	r.Push(pk(1))
	if len(r.Push(pk(3))) != 0 || len(r.Push(pk(4))) != 0 {
		t.Fatal("held packets must not be delivered")
	}
	if m := r.Missing(10); len(m) != 1 || m[0] != 2 {
		t.Fatalf("missing %v", m)
	}
	got := seqs(r.Push(pk(2)))
	if len(got) != 3 || got[0] != 2 || got[1] != 3 || got[2] != 4 || r.CumAck() != 4 {
		t.Fatalf("%v cum=%d", got, r.CumAck())
	}
}

func TestDuplicatesAndWindow(t *testing.T) {
	r := NewReorderer(4)
	r.Push(pk(1))
	r.Push(pk(1))
	r.Push(pk(3))
	r.Push(pk(3))
	r.Push(pk(100))
	if r.Dups != 2 || r.TooFar != 1 {
		t.Fatalf("dups=%d toofar=%d", r.Dups, r.TooFar)
	}
}

func TestShuffledStreamDeliversExactlyOnceInOrder(t *testing.T) {
	rng := rand.New(rand.NewSource(7))
	const n = 2000
	order := rng.Perm(n)
	r := NewReorderer(n + 1)
	var delivered []uint32
	for _, i := range order {
		s := uint32(i + 1)
		delivered = append(delivered, seqs(r.Push(pk(s)))...)
		if rng.Intn(5) == 0 {
			delivered = append(delivered, seqs(r.Push(pk(s)))...) // duplicate
		}
	}
	if len(delivered) != n {
		t.Fatalf("delivered %d of %d", len(delivered), n)
	}
	for i, s := range delivered {
		if s != uint32(i+1) {
			t.Fatalf("out of order at %d: %d", i, s)
		}
	}
}

func TestHeldPayloadSurvivesBufferReuse(t *testing.T) {
	r := NewReorderer(8)
	buf := []byte{9}
	r.Push(Packet{Seq: 2, Payload: buf})
	buf[0] = 0
	out := r.Push(pk(1))
	if len(out) != 2 || out[1].Payload[0] != 9 {
		t.Fatalf("held payload was aliased: %+v", out)
	}
}
