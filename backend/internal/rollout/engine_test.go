package rollout

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

// fakeRepo mirrors the semantics of store.PG (COALESCE updates, ordered steps) in memory.
type fakeRepo struct {
	r       store.Rollout
	steps   []*store.Step
	nodes   map[[2]int16]*store.Node
	faults  map[[2]int16][]time.Time
	tr      map[int32]*store.Transfer // by step id
	alerts  []string
	nextID  int32
	now     *time.Time
	failGet bool
}

func newRepo(now *time.Time, gws ...int16) *fakeRepo {
	f := &fakeRepo{nodes: map[[2]int16]*store.Node{}, faults: map[[2]int16][]time.Time{}, tr: map[int32]*store.Transfer{}, now: now}
	f.r = store.Rollout{ID: 1, ImageID: 1, NodeID: 2, Version: 2, State: "running", HealthWindow: 30000, SoakMS: 3000}
	for i, g := range gws {
		phase := "fleet"
		if i == 0 {
			phase = "canary"
		}
		f.steps = append(f.steps, &store.Step{ID: int32(i + 1), RolloutID: 1, Ordinal: int32(i), GatewayID: g, NodeID: 2, Phase: phase, State: "pending"})
		f.nodes[[2]int16{g, 2}] = &store.Node{GatewayID: g, NodeID: 2, Name: "body", FwVersion: 1, Slot: 0, State: 1, LastSeen: *now}
	}
	return f
}

func (f *fakeRepo) RunningRollouts(context.Context) ([]store.Rollout, error) {
	if f.r.State == "running" {
		return []store.Rollout{f.r}, nil
	}
	return nil, nil
}
func (f *fakeRepo) Steps(context.Context, int32) ([]store.Step, error) {
	out := make([]store.Step, len(f.steps))
	for i, s := range f.steps {
		out[i] = *s
	}
	return out, nil
}
func (f *fakeRepo) step(id int32) *store.Step {
	for _, s := range f.steps {
		if s.ID == id {
			return s
		}
	}
	return nil
}
func (f *fakeRepo) UpdateStep(_ context.Context, id int32, u store.StepUpdate) error {
	s := f.step(id)
	if u.State != "" {
		s.State = u.State
	}
	if u.TargetSlot != nil {
		s.TargetSlot = u.TargetSlot
	}
	if u.PrevVersion != nil {
		s.PrevVersion = u.PrevVersion
	}
	if u.SeenTarget != nil {
		s.SeenTarget = *u.SeenTarget
	}
	if u.StartedAt != nil {
		s.StartedAt = u.StartedAt
	}
	if u.TransferredAt != nil {
		s.TransferredAt = u.TransferredAt
	}
	if u.ClearHealthy {
		s.HealthySince = nil
	} else if u.HealthySince != nil {
		s.HealthySince = u.HealthySince
	}
	if u.Detail != nil {
		s.Detail = *u.Detail
	}
	return nil
}
func (f *fakeRepo) FinishRollout(_ context.Context, _ int32, state, reason string) error {
	f.r.State = state
	f.r.FailureReason = &reason
	return nil
}
func (f *fakeRepo) SkipPendingSteps(_ context.Context, _ int32, why string) error {
	for _, s := range f.steps {
		if s.State == "pending" {
			s.State, s.Detail = "skipped", why
		}
	}
	return nil
}
func (f *fakeRepo) GetNode(_ context.Context, gw, n int16) (*store.Node, error) {
	if f.failGet {
		return nil, fmt.Errorf("db down")
	}
	if x, ok := f.nodes[[2]int16{gw, n}]; ok {
		c := *x
		return &c, nil
	}
	return nil, nil
}
func (f *fakeRepo) CountFaultsSince(_ context.Context, gw, n int16, since time.Time) (int, error) {
	c := 0
	for _, t := range f.faults[[2]int16{gw, n}] {
		if !t.Before(since) {
			c++
		}
	}
	return c, nil
}
func (f *fakeRepo) ImageBlobLen(context.Context, int32, int16) (int32, error) { return 40000, nil }
func (f *fakeRepo) CreateTransfer(_ context.Context, step int32, gw, node, slot int16, ver int32, size int32) (int32, error) {
	f.nextID++
	f.tr[step] = &store.Transfer{ID: f.nextID, StepID: step, GatewayID: gw, NodeID: node, Slot: slot, Version: ver, State: "queued", Size: size, UpdatedAt: *f.now}
	return f.nextID, nil
}
func (f *fakeRepo) TransferForStep(_ context.Context, step int32) (*store.Transfer, error) {
	if t, ok := f.tr[step]; ok {
		c := *t
		return &c, nil
	}
	return nil, nil
}
func (f *fakeRepo) SetTransferState(_ context.Context, id int32, state, detail string) error {
	for _, t := range f.tr {
		if t.ID == id {
			t.State, t.Detail = state, detail
		}
	}
	return nil
}
func (f *fakeRepo) RaiseAlert(_ context.Context, rule string, gw, node int16, _, msg string) (bool, error) {
	f.alerts = append(f.alerts, rule)
	return true, nil
}

// world drives the engine and plays the part of gateways + nodes.
type world struct {
	t   *testing.T
	now time.Time
	f   *fakeRepo
	e   *Engine
}

func newWorld(t *testing.T, gws ...int16) *world {
	w := &world{t: t, now: time.Date(2026, 10, 12, 12, 0, 0, 0, time.UTC)}
	w.f = newRepo(&w.now, gws...)
	w.e = &Engine{Repo: w.f, Now: func() time.Time { return w.now }}
	return w
}

// advance moves time forward in 250 ms ticks; every node that is "alive" refreshes its heartbeat first.
func (w *world) advance(d time.Duration, alive ...int16) {
	for el := time.Duration(0); el < d; el += 250 * time.Millisecond {
		w.now = w.now.Add(250 * time.Millisecond)
		for _, g := range alive {
			w.f.nodes[[2]int16{g, 2}].LastSeen = w.now
		}
		if err := w.e.Tick(context.Background()); err != nil {
			w.t.Fatal(err)
		}
	}
}
func (w *world) node(gw int16) *store.Node { return w.f.nodes[[2]int16{gw, 2}] }
func (w *world) stepState(i int) string    { return w.f.steps[i].State }

// installNew plays a successful flash: transfer completes, node reboots into the new slot (still in trial).
func (w *world) finishTransfer(gw int16) {
	for _, s := range w.f.steps {
		if s.GatewayID == gw {
			if tr, ok := w.f.tr[s.ID]; ok {
				tr.State, tr.UpdatedAt = "transferred", w.now
			}
		}
	}
}
func (w *world) nodeRunsNew(gw int16, trial bool) {
	n := w.node(gw)
	n.FwVersion, n.Slot, n.Trial, n.State = 2, 1, trial, 1
}

func TestHappyPathCanaryThenFleet(t *testing.T) {
	w := newWorld(t, 1, 2, 3)
	for _, g := range []int16{1, 2, 3} {
		w.advance(500*time.Millisecond, 1, 2, 3)
		if w.stepState(int(g)-1) != "transferring" {
			t.Fatalf("gw %d: step state %s", g, w.stepState(int(g)-1))
		}
		if tr := w.f.tr[w.f.steps[g-1].ID]; tr == nil || tr.Slot != 1 || tr.Version != 2 {
			t.Fatalf("transfer for the inactive slot expected: %+v", tr)
		}
		for i := int(g); i < 3; i++ {
			if w.stepState(i) != "pending" {
				t.Fatalf("step %d must wait for the previous one, is %s", i, w.stepState(i))
			}
		}
		w.finishTransfer(g)
		w.advance(250*time.Millisecond, 1, 2, 3)
		w.nodeRunsNew(g, true)
		w.advance(2*time.Second, 1, 2, 3) // still in trial: must NOT be called healthy
		if w.stepState(int(g)-1) != "verifying" {
			t.Fatalf("trial node counted as healthy: %s", w.stepState(int(g)-1))
		}
		w.node(g).Trial = false
		w.advance(4*time.Second, 1, 2, 3)
		if w.stepState(int(g)-1) != "healthy" {
			t.Fatalf("gw %d not healthy after soak: %s (%s)", g, w.stepState(int(g)-1), w.f.steps[g-1].Detail)
		}
	}
	w.advance(time.Second, 1, 2, 3)
	if w.f.r.State != "succeeded" {
		t.Fatalf("rollout state %s", w.f.r.State)
	}
}

func TestCanaryThatNeverBecomesHealthyHaltsRolloutAndLeavesFleetUntouched(t *testing.T) {
	w := newWorld(t, 1, 2, 3)
	w.advance(500*time.Millisecond, 1, 2, 3)
	w.finishTransfer(1)
	w.advance(250*time.Millisecond, 1, 2, 3)
	w.nodeRunsNew(1, true) // boots the new image but never confirms
	w.advance(40*time.Second, 1, 2, 3)
	if w.f.r.State != "failed" || w.stepState(0) != "failed" {
		t.Fatalf("rollout=%s canary=%s", w.f.r.State, w.stepState(0))
	}
	if w.stepState(1) != "skipped" || w.stepState(2) != "skipped" {
		t.Fatal("rest of the fleet must be skipped")
	}
	if len(w.f.tr) != 1 {
		t.Fatalf("fleet nodes must not receive transfers (%d transfers)", len(w.f.tr))
	}
	if len(w.f.alerts) == 0 {
		t.Fatal("no alert raised")
	}
}

func TestBootloaderRollbackIsDetected(t *testing.T) {
	w := newWorld(t, 1, 2)
	w.advance(500*time.Millisecond, 1, 2)
	w.finishTransfer(1)
	w.advance(250*time.Millisecond, 1, 2)
	w.nodeRunsNew(1, true)
	w.advance(time.Second, 1, 2)
	n := w.node(1) // trial image failed its health check; bootloader falls back
	n.FwVersion, n.Slot, n.Trial = 1, 0, false
	w.advance(time.Second, 1, 2)
	if w.stepState(0) != "rolled_back" || w.f.r.State != "failed" || w.stepState(1) != "skipped" {
		t.Fatalf("step=%s rollout=%s second=%s", w.stepState(0), w.f.r.State, w.stepState(1))
	}
}

func TestFailClosedWhenNodeStopsReportingAfterUpdate(t *testing.T) {
	w := newWorld(t, 1, 2)
	w.advance(500*time.Millisecond, 1, 2)
	w.finishTransfer(1)
	w.nodeRunsNew(1, false)
	w.advance(time.Second, 1, 2)
	w.advance(40*time.Second, 2) // canary goes silent: no heartbeats, no evidence of health
	if w.stepState(0) != "failed" || w.f.r.State != "failed" {
		t.Fatalf("silence must fail the gate: step=%s rollout=%s", w.stepState(0), w.f.r.State)
	}
}

func TestFaultsDuringVerificationFailTheGate(t *testing.T) {
	w := newWorld(t, 1, 2)
	w.advance(500*time.Millisecond, 1, 2)
	w.finishTransfer(1)
	w.advance(250*time.Millisecond, 1, 2)
	w.nodeRunsNew(1, false)
	w.f.faults[[2]int16{1, 2}] = []time.Time{w.now}
	w.advance(40*time.Second, 1, 2)
	if w.stepState(0) != "failed" {
		t.Fatalf("faults since the update were ignored: %s", w.stepState(0))
	}
}

func TestFlappingHealthRestartsTheSoak(t *testing.T) {
	w := newWorld(t, 1)
	w.advance(500*time.Millisecond, 1)
	w.finishTransfer(1)
	w.advance(250*time.Millisecond, 1)
	w.nodeRunsNew(1, false)
	w.advance(2*time.Second, 1) // 2 s healthy of the required 3 s
	w.node(1).State = 3         // FAULT for a moment
	w.advance(500*time.Millisecond, 1)
	w.node(1).State = 1
	w.advance(2*time.Second, 1)
	if w.stepState(0) != "verifying" {
		t.Fatalf("soak must restart after a flap, got %s", w.stepState(0))
	}
	w.advance(2*time.Second, 1)
	if w.stepState(0) != "healthy" {
		t.Fatalf("expected healthy after a full clean soak, got %s", w.stepState(0))
	}
}

func TestRestartedEngineResumesFromDatabaseState(t *testing.T) {
	w := newWorld(t, 1, 2)
	w.advance(500*time.Millisecond, 1, 2)
	w.finishTransfer(1)
	w.advance(250*time.Millisecond, 1, 2)
	w.nodeRunsNew(1, false)
	w.advance(1500*time.Millisecond, 1, 2)                           // soak in progress
	w.e = &Engine{Repo: w.f, Now: func() time.Time { return w.now }} // backend process restarted: new engine, same database
	w.advance(2500*time.Millisecond, 1, 2)
	if w.stepState(0) != "healthy" {
		t.Fatalf("restart lost progress: %s", w.stepState(0))
	}
	w.advance(500*time.Millisecond, 1, 2)
	if w.stepState(1) != "transferring" {
		t.Fatalf("did not continue to the next node: %s", w.stepState(1))
	}
}

func TestRefusesToStartOnUnseenOrUnsuitableNodes(t *testing.T) {
	cases := map[string]func(w *world){
		"not reporting": func(w *world) { w.now = w.now.Add(time.Minute) },
		"in trial":      func(w *world) { w.node(1).Trial = true },
		"faulted":       func(w *world) { w.node(1).State = 3 },
		"image too old": func(w *world) { w.node(1).FwVersion = 2 },
	}
	for name, setup := range cases {
		t.Run(name, func(t *testing.T) {
			w := newWorld(t, 1, 2)
			setup(w)
			if name != "not reporting" {
				w.f.nodes[[2]int16{1, 2}].LastSeen = w.now
			}
			w.e.Tick(context.Background())
			if w.stepState(0) != "failed" || w.f.r.State != "failed" || len(w.f.tr) != 0 {
				t.Fatalf("step=%s rollout=%s transfers=%d", w.stepState(0), w.f.r.State, len(w.f.tr))
			}
		})
	}
}

func TestTransferFailureAndStallFailTheStep(t *testing.T) {
	for _, mode := range []string{"verify_failed", "stall"} {
		w := newWorld(t, 1)
		w.advance(500*time.Millisecond, 1)
		tr := w.f.tr[w.f.steps[0].ID]
		if mode == "verify_failed" {
			tr.State = "verify_failed"
			w.advance(500*time.Millisecond, 1)
		} else {
			w.advance(2*time.Minute, 1)
		}
		if w.stepState(0) != "failed" || w.f.r.State != "failed" {
			t.Fatalf("%s: step=%s rollout=%s", mode, w.stepState(0), w.f.r.State)
		}
	}
}

func TestDatabaseErrorDoesNotAdvanceOrFailTheRollout(t *testing.T) {
	w := newWorld(t, 1)
	w.f.failGet = true
	w.advance(2*time.Second, 1)
	if w.stepState(0) != "pending" || w.f.r.State != "running" {
		t.Fatalf("transient db error changed state: %s %s", w.stepState(0), w.f.r.State)
	}
	w.f.failGet = false
	w.advance(500*time.Millisecond, 1)
	if w.stepState(0) != "transferring" {
		t.Fatal("did not recover")
	}
}
