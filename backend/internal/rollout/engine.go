// Package rollout is the staged-rollout orchestrator: canary first, then the rest of the fleet one node at a time.
// Promotion is gated on health read from the database, and the gate is FAIL-CLOSED: a node that stops reporting, reverts
// to its old image, shows new faults, or simply does not become healthy inside the window fails the step, halts the
// rollout and raises an alert. All state lives in Postgres, so a restarted engine picks up exactly where it was.
package rollout

import (
	"context"
	"fmt"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/canmsg"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type Repo interface {
	RunningRollouts(ctx context.Context) ([]store.Rollout, error)
	Steps(ctx context.Context, rolloutID int32) ([]store.Step, error)
	UpdateStep(ctx context.Context, id int32, u store.StepUpdate) error
	FinishRollout(ctx context.Context, id int32, state, reason string) error
	SkipPendingSteps(ctx context.Context, rolloutID int32, why string) error
	GetNode(ctx context.Context, gw, node int16) (*store.Node, error)
	CountFaultsSince(ctx context.Context, gw, node int16, since time.Time) (int, error)
	ImageBlobLen(ctx context.Context, imageID int32, slot int16) (int32, error)
	CreateTransfer(ctx context.Context, stepID int32, gw, node, slot int16, version int32, size int32) (int32, error)
	TransferForStep(ctx context.Context, stepID int32) (*store.Transfer, error)
	SetTransferState(ctx context.Context, id int32, state, detail string) error
	RaiseAlert(ctx context.Context, rule string, gw, node int16, severity, msg string) (bool, error)
}

type Config struct {
	Fresh         time.Duration // a node's last heartbeat must be this recent to count as reporting
	TransferStall time.Duration // a transfer with no progress for this long fails the step
}

func (c *Config) defaults() {
	if c.Fresh == 0 {
		c.Fresh = 1500 * time.Millisecond
	}
	if c.TransferStall == 0 {
		c.TransferStall = 90 * time.Second
	}
}

type Engine struct {
	Repo Repo
	Cfg  Config
	Now  func() time.Time
	Logf func(string, ...any)
}

func (e *Engine) logf(f string, a ...any) {
	if e.Logf != nil {
		e.Logf(f, a...)
	}
}

// Tick advances every running rollout by at most one step transition. It is idempotent and holds no state.
func (e *Engine) Tick(ctx context.Context) error {
	e.Cfg.defaults()
	rs, err := e.Repo.RunningRollouts(ctx)
	if err != nil {
		return err
	}
	for i := range rs {
		if err := e.tickRollout(ctx, &rs[i]); err != nil {
			e.logf("rollout %d: %v", rs[i].ID, err)
		}
	}
	return nil
}

func (e *Engine) fresh(n *store.Node) bool { return n != nil && e.Now().Sub(n.LastSeen) <= e.Cfg.Fresh }

func (e *Engine) tickRollout(ctx context.Context, r *store.Rollout) error {
	steps, err := e.Repo.Steps(ctx, r.ID)
	if err != nil {
		return err
	}
	var cur *store.Step
	for i := range steps {
		if s := &steps[i]; s.State != "healthy" && s.State != "skipped" {
			cur = s
			break
		}
	}
	if cur == nil {
		e.logf("rollout %d: all steps healthy", r.ID)
		return e.Repo.FinishRollout(ctx, r.ID, "succeeded", "")
	}
	switch cur.State {
	case "failed", "rolled_back": // crashed between recording the step and finishing the rollout
		return e.halt(ctx, r, cur, cur.Detail)
	case "pending":
		return e.startStep(ctx, r, cur)
	case "transferring":
		return e.checkTransfer(ctx, r, cur)
	case "verifying":
		return e.checkHealth(ctx, r, cur)
	}
	return fmt.Errorf("step %d in unknown state %q", cur.ID, cur.State)
}

func (e *Engine) failStep(ctx context.Context, r *store.Rollout, s *store.Step, state, why string) error {
	e.logf("rollout %d: step gw=%d node=%d %s: %s", r.ID, s.GatewayID, s.NodeID, state, why)
	if err := e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{State: state, Detail: &why}); err != nil {
		return err
	}
	return e.halt(ctx, r, s, why)
}

func (e *Engine) halt(ctx context.Context, r *store.Rollout, s *store.Step, why string) error {
	if err := e.Repo.SkipPendingSteps(ctx, r.ID, fmt.Sprintf("rollout halted after %s step on gateway %d failed", s.Phase, s.GatewayID)); err != nil {
		return err
	}
	e.Repo.RaiseAlert(ctx, "rollout_failed", s.GatewayID, s.NodeID, "critical", fmt.Sprintf("rollout %d halted (%s step, gateway %d): %s", r.ID, s.Phase, s.GatewayID, why))
	return e.Repo.FinishRollout(ctx, r.ID, "failed", fmt.Sprintf("%s step on gateway %d: %s", s.Phase, s.GatewayID, why))
}

func (e *Engine) startStep(ctx context.Context, r *store.Rollout, s *store.Step) error {
	n, err := e.Repo.GetNode(ctx, s.GatewayID, s.NodeID)
	if err != nil {
		return err
	}
	switch {
	case !e.fresh(n):
		return e.failStep(ctx, r, s, "failed", "node is not reporting; refusing to update a node we cannot see")
	case n.Trial:
		return e.failStep(ctx, r, s, "failed", "node is still in trial boot of a previous image")
	case n.State == canmsg.StateFault || n.State == canmsg.StateSafe || n.State == canmsg.StateUpdating || n.State == canmsg.StateBoot:
		return e.failStep(ctx, r, s, "failed", "node is "+canmsg.StateName(int(n.State))+"; refusing to update")
	case r.Version <= n.FwVersion:
		return e.failStep(ctx, r, s, "failed", fmt.Sprintf("image v%d is not newer than running v%d", r.Version, n.FwVersion))
	}
	slot := 1 - n.Slot
	size, err := e.Repo.ImageBlobLen(ctx, r.ImageID, slot)
	if err != nil {
		return err
	}
	if _, err := e.Repo.CreateTransfer(ctx, s.ID, s.GatewayID, s.NodeID, slot, r.Version, size); err != nil {
		return err
	}
	now, prev := e.Now(), n.FwVersion
	detail := fmt.Sprintf("updating v%d -> v%d into slot %c", prev, r.Version, 'A'+slot)
	return e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{State: "transferring", TargetSlot: &slot, PrevVersion: &prev, StartedAt: &now, Detail: &detail})
}

func (e *Engine) checkTransfer(ctx context.Context, r *store.Rollout, s *store.Step) error {
	t, err := e.Repo.TransferForStep(ctx, s.ID)
	if err != nil {
		return err
	}
	if t == nil {
		return e.failStep(ctx, r, s, "failed", "transfer record missing")
	}
	switch t.State {
	case "transferred":
		now := e.Now()
		detail := "image written and verified by the node; waiting for it to reboot and prove healthy"
		return e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{State: "verifying", TransferredAt: &now, ClearHealthy: true, Detail: &detail})
	case "failed", "verify_failed", "cancelled":
		return e.failStep(ctx, r, s, "failed", "transfer "+t.State+": "+t.Detail)
	}
	if e.Now().Sub(t.UpdatedAt) > e.Cfg.TransferStall {
		e.Repo.SetTransferState(ctx, t.ID, "failed", "no progress for "+e.Cfg.TransferStall.String())
		return e.failStep(ctx, r, s, "failed", "transfer stalled")
	}
	return nil
}

// healthy reports whether the node currently meets every part of the gate, and if not, why.
func (e *Engine) healthy(ctx context.Context, s *store.Step, n *store.Node, target int32) (bool, string) {
	switch {
	case n == nil:
		return false, "node unknown"
	case !e.fresh(n):
		return false, "node not reporting"
	case n.FwVersion != target || s.TargetSlot == nil || n.Slot != *s.TargetSlot:
		return false, fmt.Sprintf("node reports v%d slot %d, want v%d slot %d", n.FwVersion, n.Slot, target, derefSlot(s.TargetSlot))
	case n.Trial:
		return false, "node has not confirmed the new image yet (still in trial boot)"
	case n.State != canmsg.StateNormal && n.State != canmsg.StateWarning:
		return false, "node state " + canmsg.StateName(int(n.State))
	}
	if s.TransferredAt != nil {
		if c, err := e.Repo.CountFaultsSince(ctx, s.GatewayID, s.NodeID, *s.TransferredAt); err != nil {
			return false, "cannot read faults: " + err.Error() // fail closed
		} else if c > 0 {
			return false, fmt.Sprintf("%d fault event(s) since the update", c)
		}
	}
	return true, ""
}

func derefSlot(p *int16) int16 {
	if p == nil {
		return -1
	}
	return *p
}

func (e *Engine) checkHealth(ctx context.Context, r *store.Rollout, s *store.Step) error {
	n, err := e.Repo.GetNode(ctx, s.GatewayID, s.NodeID)
	if err != nil {
		return err
	}
	now := e.Now()
	if s.TransferredAt == nil {
		return e.failStep(ctx, r, s, "failed", "internal: verifying without a transfer time")
	}
	elapsed := now.Sub(*s.TransferredAt)

	onTarget := n != nil && n.FwVersion == r.Version && s.TargetSlot != nil && n.Slot == *s.TargetSlot
	if onTarget && !s.SeenTarget {
		t := true
		if err := e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{SeenTarget: &t}); err != nil {
			return err
		}
		s.SeenTarget = true
	}
	// The node ran the new image and then went back to the old one: the bootloader rolled it back.
	if s.SeenTarget && e.fresh(n) && s.PrevVersion != nil && n.FwVersion == *s.PrevVersion && !onTarget {
		return e.failStep(ctx, r, s, "rolled_back", fmt.Sprintf("node ran v%d but rolled back to v%d (bootloader health check failed)", r.Version, n.FwVersion))
	}

	ok, why := e.healthy(ctx, s, n, r.Version)
	window := time.Duration(r.HealthWindow) * time.Millisecond
	soak := time.Duration(r.SoakMS) * time.Millisecond
	switch {
	case ok && s.HealthySince == nil:
		return e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{HealthySince: &now, Detail: strp("healthy; soaking")})
	case ok && now.Sub(*s.HealthySince) >= soak:
		d := fmt.Sprintf("v%d healthy for %s", r.Version, now.Sub(*s.HealthySince).Round(100*time.Millisecond))
		return e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{State: "healthy", Detail: &d})
	case !ok && s.HealthySince != nil: // flapped: restart the soak
		if err := e.Repo.UpdateStep(ctx, s.ID, store.StepUpdate{ClearHealthy: true, Detail: strp("health lost during soak: " + why)}); err != nil {
			return err
		}
	}
	if !ok && elapsed > window {
		if s.SeenTarget {
			return e.failStep(ctx, r, s, "failed", fmt.Sprintf("health window (%s) expired: %s", window, why))
		}
		return e.failStep(ctx, r, s, "failed", fmt.Sprintf("health window (%s) expired and the node never ran v%d: %s", window, r.Version, why))
	}
	return nil
}

func strp(s string) *string { return &s }

func (e *Engine) Run(ctx context.Context, every time.Duration) {
	t := time.NewTicker(every)
	defer t.Stop()
	for {
		if err := e.Tick(ctx); err != nil && ctx.Err() == nil {
			e.logf("rollout engine: %v", err)
		}
		select {
		case <-t.C:
		case <-ctx.Done():
			return
		}
	}
}
