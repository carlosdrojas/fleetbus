// Package ota drives firmware transfers from the database to gateways over FBP. A transfer row (created by the rollout
// engine) is offered to the gateway, streamed as OTA_CHUNKs inside a bounded window, and finished by the OTA_STATUS
// packets the ingest writer records. Everything is derived from the row, so a restart simply resumes.
package ota

import (
	"context"
	"fmt"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type Repo interface {
	ActiveTransfers(ctx context.Context) ([]store.Transfer, error)
	TransferBlob(ctx context.Context, id int32) ([]byte, error)
	SetTransferState(ctx context.Context, id int32, state, detail string) error
	CancelledTransfers(ctx context.Context, within time.Duration) ([]store.Transfer, error)
}

type Sender interface {
	Send(gateway int16, ptype uint8, payload []byte) error
}

type Config struct {
	ChunkBytes   int           // payload bytes per OTA_CHUNK
	WindowChunks int           // chunks in flight beyond what the gateway has confirmed
	ResendAfter  time.Duration // silence before going back to the confirmed offset
	StallTimeout time.Duration // no progress at all -> fail the transfer
}

func (c *Config) defaults() {
	if c.ChunkBytes == 0 {
		c.ChunkBytes = 900
	}
	if c.WindowChunks == 0 {
		c.WindowChunks = 8
	}
	if c.ResendAfter == 0 {
		c.ResendAfter = 1500 * time.Millisecond
	}
	if c.StallTimeout == 0 {
		c.StallTimeout = 60 * time.Second
	}
}

type session struct {
	blob         []byte
	sentTo       int
	confirmed    int
	lastProgress time.Time
	offered      bool
}

type Driver struct {
	Repo Repo
	Out  Sender
	Cfg  Config
	Now  func() time.Time
	Logf func(string, ...any)

	sessions   map[int32]*session
	cancelSent map[int32]bool
}

func (d *Driver) logf(f string, a ...any) {
	if d.Logf != nil {
		d.Logf(f, a...)
	}
}

// Tick advances every active transfer once.
func (d *Driver) Tick(ctx context.Context) error {
	d.Cfg.defaults()
	if d.sessions == nil {
		d.sessions = map[int32]*session{}
		d.cancelSent = map[int32]bool{}
	}
	if cs, err := d.Repo.CancelledTransfers(ctx, time.Minute); err == nil {
		for _, t := range cs {
			if !d.cancelSent[t.ID] {
				d.cancelOnGateway(t)
				d.cancelSent[t.ID] = true
			}
		}
	}
	ts, err := d.Repo.ActiveTransfers(ctx)
	if err != nil {
		return err
	}
	live := map[int32]bool{}
	now := d.Now()
	for _, t := range ts {
		live[t.ID] = true
		s := d.sessions[t.ID]
		if s == nil {
			blob, err := d.Repo.TransferBlob(ctx, t.ID)
			if err != nil {
				d.Repo.SetTransferState(ctx, t.ID, "failed", "image unavailable: "+err.Error())
				continue
			}
			s = &session{blob: blob, lastProgress: now, confirmed: int(t.NextOffset), sentTo: int(t.NextOffset)}
			d.sessions[t.ID] = s
		}
		if int(t.NextOffset) > s.confirmed {
			s.confirmed, s.lastProgress = int(t.NextOffset), now
		}
		if now.Sub(s.lastProgress) > d.Cfg.StallTimeout {
			d.Repo.SetTransferState(ctx, t.ID, "failed", fmt.Sprintf("stalled at byte %d of %d", s.confirmed, len(s.blob)))
			d.cancelOnGateway(t)
			delete(d.sessions, t.ID)
			continue
		}
		if !s.offered || (s.confirmed == 0 && now.Sub(s.lastProgress) > d.Cfg.ResendAfter) {
			offer := fbp.EncodeOtaOffer(fbp.OtaOffer{Node: uint8(t.NodeID), Slot: uint8(t.Slot), Version: uint32(t.Version), Size: uint32(len(s.blob)), ImageID: uint32(t.ID)})
			if err := d.Out.Send(t.GatewayID, fbp.TypeOtaOffer, offer); err != nil {
				d.logf("ota: transfer %d: offer not sent: %v", t.ID, err)
				continue
			}
			s.offered, s.lastProgress = true, now
			if t.State == "queued" {
				d.Repo.SetTransferState(ctx, t.ID, "active", "")
			}
		}
		if now.Sub(s.lastProgress) > d.Cfg.ResendAfter && s.sentTo > s.confirmed {
			s.sentTo = s.confirmed // silence: resend from what the gateway has confirmed
			s.lastProgress = now
		}
		limit := s.confirmed + d.Cfg.WindowChunks*d.Cfg.ChunkBytes
		for s.sentTo < len(s.blob) && s.sentTo < limit {
			end := s.sentTo + d.Cfg.ChunkBytes
			if end > len(s.blob) {
				end = len(s.blob)
			}
			if err := d.Out.Send(t.GatewayID, fbp.TypeOtaChunk, fbp.EncodeOtaChunk(uint8(t.NodeID), uint32(s.sentTo), s.blob[s.sentTo:end])); err != nil {
				break
			}
			s.sentTo = end
		}
	}
	for id := range d.sessions {
		if !live[id] {
			delete(d.sessions, id)
		}
	}
	return nil
}

func (d *Driver) cancelOnGateway(t store.Transfer) {
	d.Out.Send(t.GatewayID, fbp.TypeOtaOffer, fbp.EncodeOtaOffer(fbp.OtaOffer{Node: uint8(t.NodeID), Slot: uint8(t.Slot), ImageID: uint32(t.ID)})) // size 0 = cancel
}

// Cancel tells the gateway to drop a transfer (used when a rollout is aborted).
func (d *Driver) Cancel(t store.Transfer) { d.cancelOnGateway(t) }

func (d *Driver) Run(ctx context.Context, every time.Duration) {
	tk := time.NewTicker(every)
	defer tk.Stop()
	for {
		if err := d.Tick(ctx); err != nil && ctx.Err() == nil {
			d.logf("ota: %v", err)
		}
		select {
		case <-tk.C:
		case <-ctx.Done():
			return
		}
	}
}
