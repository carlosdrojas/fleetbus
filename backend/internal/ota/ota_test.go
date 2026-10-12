package ota

import (
	"bytes"
	"context"
	"testing"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type fakeRepo struct {
	ts        []store.Transfer
	blob      []byte
	cancelled []store.Transfer
}

func (f *fakeRepo) ActiveTransfers(context.Context) ([]store.Transfer, error) {
	var out []store.Transfer
	for _, t := range f.ts {
		if t.State == "queued" || t.State == "active" {
			out = append(out, t)
		}
	}
	return out, nil
}
func (f *fakeRepo) TransferBlob(context.Context, int32) ([]byte, error) { return f.blob, nil }
func (f *fakeRepo) SetTransferState(_ context.Context, id int32, state, detail string) error {
	for i := range f.ts {
		if f.ts[i].ID == id {
			f.ts[i].State, f.ts[i].Detail = state, detail
		}
	}
	return nil
}
func (f *fakeRepo) CancelledTransfers(context.Context, time.Duration) ([]store.Transfer, error) {
	return f.cancelled, nil
}

type sent struct {
	gw    int16
	ptype uint8
	pl    []byte
}
type fakeOut struct{ pkts []sent }

func (o *fakeOut) Send(gw int16, ptype uint8, pl []byte) error {
	o.pkts = append(o.pkts, sent{gw, ptype, append([]byte(nil), pl...)})
	return nil
}
func (o *fakeOut) take(ptype uint8) []sent {
	var r, keep []sent
	for _, p := range o.pkts {
		if p.ptype == ptype {
			r = append(r, p)
		} else {
			keep = append(keep, p)
		}
	}
	o.pkts = keep
	return r
}

func setup(size int) (*fakeRepo, *fakeOut, *Driver, *time.Time) {
	blob := make([]byte, size)
	for i := range blob {
		blob[i] = byte(i * 7)
	}
	now := time.Date(2026, 10, 12, 0, 0, 0, 0, time.UTC)
	r := &fakeRepo{blob: blob, ts: []store.Transfer{{ID: 5, GatewayID: 3, NodeID: 2, Slot: 1, Version: 2, State: "queued", Size: int32(size)}}}
	o := &fakeOut{}
	return r, o, &Driver{Repo: r, Out: o, Now: func() time.Time { return now }}, &now
}

func TestOffersThenStreamsWithinWindow(t *testing.T) {
	r, o, d, _ := setup(20000)
	d.Tick(context.Background())
	offers := o.take(fbp.TypeOtaOffer)
	if len(offers) != 1 || offers[0].gw != 3 {
		t.Fatalf("offers: %+v", offers)
	}
	off, _ := fbp.DecodeOtaOffer(offers[0].pl)
	if off.Node != 2 || off.Slot != 1 || off.Size != 20000 || off.ImageID != 5 || off.Version != 2 {
		t.Fatalf("%+v", off)
	}
	if r.ts[0].State != "active" {
		t.Fatalf("state %s", r.ts[0].State)
	}
	chunks := o.take(fbp.TypeOtaChunk)
	if len(chunks) != 8 {
		t.Fatalf("window should allow 8 chunks, sent %d", len(chunks))
	}
	d.Tick(context.Background())
	if len(o.take(fbp.TypeOtaChunk)) != 0 {
		t.Fatal("must not send past the window until the gateway confirms bytes")
	}
}

func TestConfirmationsAdvanceWindowAndStreamReassembles(t *testing.T) {
	r, o, d, now := setup(5000)
	var got []byte
	ctx := context.Background()
	for i := 0; i < 50 && len(got) < 5000; i++ {
		d.Tick(ctx)
		for _, c := range o.take(fbp.TypeOtaChunk) {
			_, off, data, _ := fbp.DecodeOtaChunk(c.pl)
			if int(off) == len(got) {
				got = append(got, data...)
			}
		}
		r.ts[0].NextOffset = int32(len(got)) // gateway's PROGRESS statuses, recorded by ingest
		*now = now.Add(100 * time.Millisecond)
	}
	if !bytes.Equal(got, r.blob) {
		t.Fatalf("reassembled %d bytes, mismatch", len(got))
	}
}

func TestSilenceRewindsToConfirmedOffset(t *testing.T) {
	r, o, d, now := setup(20000)
	ctx := context.Background()
	d.Tick(ctx)
	o.take(fbp.TypeOtaChunk)
	r.ts[0].NextOffset = 1800 // gateway got two chunks, then everything after was lost
	d.Tick(ctx)
	o.take(fbp.TypeOtaChunk)
	*now = now.Add(2 * time.Second)
	d.Tick(ctx)
	chunks := o.take(fbp.TypeOtaChunk)
	if len(chunks) == 0 {
		t.Fatal("nothing resent after silence")
	}
	_, off, _, _ := fbp.DecodeOtaChunk(chunks[0].pl)
	if off != 1800 {
		t.Fatalf("resent from %d, want 1800", off)
	}
}

func TestLostOfferIsRepeated(t *testing.T) {
	_, o, d, now := setup(3000)
	d.Tick(context.Background())
	o.take(fbp.TypeOtaOffer)
	*now = now.Add(2 * time.Second)
	d.Tick(context.Background())
	if len(o.take(fbp.TypeOtaOffer)) != 1 {
		t.Fatal("offer not repeated while the gateway has confirmed nothing")
	}
}

func TestStalledTransferFailsAndCancelsOnGateway(t *testing.T) {
	r, o, d, now := setup(20000)
	d.Tick(context.Background())
	o.pkts = nil
	*now = now.Add(61 * time.Second)
	d.Tick(context.Background())
	if r.ts[0].State != "failed" {
		t.Fatalf("state %s", r.ts[0].State)
	}
	offers := o.take(fbp.TypeOtaOffer)
	if len(offers) == 0 {
		t.Fatal("no cancel sent")
	}
	if off, _ := fbp.DecodeOtaOffer(offers[len(offers)-1].pl); off.Size != 0 {
		t.Fatalf("cancel must carry size 0: %+v", off)
	}
}

func TestAbortedTransferIsCancelledOnGatewayOnce(t *testing.T) {
	r, o, d, _ := setup(20000)
	r.cancelled = []store.Transfer{{ID: 9, GatewayID: 4, NodeID: 2, Slot: 1, State: "cancelled"}}
	d.Tick(context.Background())
	d.Tick(context.Background())
	n := 0
	for _, p := range o.take(fbp.TypeOtaOffer) {
		if p.gw == 4 {
			n++
			if off, _ := fbp.DecodeOtaOffer(p.pl); off.Size != 0 {
				t.Fatal("cancel must have size 0")
			}
		}
	}
	if n != 1 {
		t.Fatalf("cancel sent %d times", n)
	}
}
