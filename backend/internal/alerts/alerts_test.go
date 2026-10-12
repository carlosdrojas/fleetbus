package alerts

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type fake struct {
	nodes  []store.Node
	gws    []store.Gateway
	faults map[[2]int16]int
	open   map[string]string
}

func (f *fake) ListNodes(context.Context) ([]store.Node, error)       { return f.nodes, nil }
func (f *fake) ListGateways(context.Context) ([]store.Gateway, error) { return f.gws, nil }
func (f *fake) CountFaultsSince(_ context.Context, gw, n int16, _ time.Time) (int, error) {
	return f.faults[[2]int16{gw, n}], nil
}
func (f *fake) RaiseAlert(_ context.Context, rule string, gw, n int16, sev, msg string) (bool, error) {
	k := fmt.Sprintf("%s/%d/%d", rule, gw, n)
	_, had := f.open[k]
	f.open[k] = msg
	return !had, nil
}
func (f *fake) ClearAlert(_ context.Context, rule string, gw, n int16) error {
	delete(f.open, fmt.Sprintf("%s/%d/%d", rule, gw, n))
	return nil
}

func setup() (*fake, *Engine, *time.Time) {
	t0 := time.Date(2026, 10, 12, 12, 0, 0, 0, time.UTC)
	now := t0
	f := &fake{open: map[string]string{}, faults: map[[2]int16]int{}}
	f.gws = []store.Gateway{{ID: 1, LastSeen: t0}}
	f.nodes = []store.Node{{GatewayID: 1, NodeID: 1, Name: "battery", State: 1, LastSeen: t0}, {GatewayID: 1, NodeID: 2, Name: "body", State: 1, LastSeen: t0}}
	return f, &Engine{Repo: f, Now: func() time.Time { return now }}, &now
}

func TestHealthyFleetRaisesNothing(t *testing.T) {
	f, e, _ := setup()
	e.Evaluate(context.Background())
	if len(f.open) != 0 {
		t.Fatalf("%v", f.open)
	}
}

func TestSilentNodeAlertsAfterThresholdAndClearsOnReturn(t *testing.T) {
	f, e, now := setup()
	*now = now.Add(1500 * time.Millisecond)
	f.gws[0].LastSeen = *now
	f.nodes[1].LastSeen = *now // body keeps reporting, battery stops
	e.Evaluate(context.Background())
	if len(f.open) != 0 {
		t.Fatalf("1.5 s is below the threshold: %v", f.open)
	}
	*now = now.Add(1 * time.Second)
	f.gws[0].LastSeen, f.nodes[1].LastSeen = *now, *now
	e.Evaluate(context.Background())
	if _, ok := f.open["node_silent/1/1"]; !ok || len(f.open) != 1 {
		t.Fatalf("expected only battery silent: %v", f.open)
	}
	f.nodes[0].LastSeen = *now
	e.Evaluate(context.Background())
	if len(f.open) != 0 {
		t.Fatalf("not cleared: %v", f.open)
	}
}

func TestGatewayOutageRaisesOneAlertNotOnePerNode(t *testing.T) {
	f, e, now := setup()
	*now = now.Add(10 * time.Second)
	e.Evaluate(context.Background())
	if len(f.open) != 1 || f.open["gateway_silent/1/0"] == "" {
		t.Fatalf("%v", f.open)
	}
}

func TestFaultAndLimpStatesAndSpike(t *testing.T) {
	f, e, _ := setup()
	f.nodes[0].State = 3 // FAULT
	f.nodes[1].State = 5 // LIMP_HOME
	f.faults[[2]int16{1, 1}] = 3
	e.Evaluate(context.Background())
	for _, k := range []string{"node_fault/1/1", "node_limp_home/1/2", "fault_spike/1/1"} {
		if _, ok := f.open[k]; !ok {
			t.Errorf("missing %s in %v", k, f.open)
		}
	}
	f.nodes[0].State, f.nodes[1].State = 1, 1
	f.faults[[2]int16{1, 1}] = 0
	e.Evaluate(context.Background())
	if len(f.open) != 0 {
		t.Fatalf("not cleared: %v", f.open)
	}
}
