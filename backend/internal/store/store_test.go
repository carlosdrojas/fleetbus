package store_test

import (
	"context"
	"testing"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/store"
	"github.com/carlosdrojas/fleetbus/backend/internal/store/storetest"
)

func hb(gw, node int16, fw int32, slot int16, trial bool, state int16, ts time.Time) store.NodeHeartbeat {
	return store.NodeHeartbeat{TS: ts, GatewayID: gw, NodeID: node, FwVersion: fw, Slot: slot, Trial: trial, State: state}
}

func TestMigrateIsIdempotent(t *testing.T) {
	pg := storetest.New(t)
	if err := pg.Migrate(context.Background()); err != nil {
		t.Fatal(err)
	}
}

func TestWriteBatchAndNodeEvents(t *testing.T) {
	pg := storetest.New(t)
	ctx := context.Background()
	now := time.Now()
	nid := int16(1)
	err := pg.WriteBatch(ctx, store.Batch{
		Gateways:   []store.GatewayUpdate{{ID: 1, Addr: "127.0.0.1:1", TS: now, Simulated: true}},
		Telemetry:  []store.TelRow{{TS: now, GatewayID: 1, NodeID: &nid, CanID: 0x200, Data: []byte{1, 2, 3, 4, 5, 6, 7, 8}}, {TS: now.Add(-48 * time.Hour), GatewayID: 1, CanID: 0x100, Data: []byte{0}}},
		Metrics:    []store.MetricRow{{TS: now, GatewayID: 1, NodeID: 1, Name: "pack_mv", Value: 3800}},
		Faults:     []store.FaultRow{{TS: now, GatewayID: 1, NodeID: 1, Code: 0x10, Severity: 2}},
		Heartbeats: []store.NodeHeartbeat{hb(1, 1, 1, 0, false, 1, now), hb(1, 1, 1, 0, false, 3, now.Add(time.Second))},
	})
	if err != nil {
		t.Fatal(err)
	}
	nodes, _ := pg.ListNodes(ctx)
	if len(nodes) != 1 || nodes[0].State != 3 || nodes[0].Name != "battery" {
		t.Fatalf("nodes: %+v", nodes)
	}
	ev, _ := pg.NodeEvents(ctx, 10)
	kinds := map[string]int{}
	for _, e := range ev {
		kinds[e.Kind]++
	}
	if kinds["first_seen"] != 1 || kinds["state"] != 1 {
		t.Fatalf("events: %v", kinds)
	}
	var parts int
	pg.Pool.QueryRow(ctx, `SELECT count(*) FROM pg_inherits WHERE inhparent='telemetry'::regclass`).Scan(&parts)
	if parts < 3 { // default + today + two days ago
		t.Fatalf("expected daily partitions, have %d", parts)
	}
	if m, _ := pg.Metrics(ctx, 1, 1, "pack_mv", now.Add(-time.Minute), 10); len(m) != 1 {
		t.Fatalf("metrics: %v", m)
	}
	if n, _ := pg.CountFaultsSince(ctx, 1, 1, now.Add(-time.Minute)); n != 1 {
		t.Fatalf("faults: %d", n)
	}
}

func TestLateHeartbeatDoesNotRollBackNodeState(t *testing.T) {
	pg := storetest.New(t)
	ctx := context.Background()
	now := time.Now()
	pg.WriteBatch(ctx, store.Batch{Gateways: []store.GatewayUpdate{{ID: 1, TS: now}}, Heartbeats: []store.NodeHeartbeat{hb(1, 2, 2, 1, false, 1, now)}})
	pg.WriteBatch(ctx, store.Batch{Heartbeats: []store.NodeHeartbeat{hb(1, 2, 1, 0, false, 5, now.Add(-time.Minute))}}) // replayed from an outage
	n, _ := pg.GetNode(ctx, 1, 2)
	if n.FwVersion != 2 || n.State != 1 {
		t.Fatalf("stale replay overwrote current state: %+v", n)
	}
}

func TestBatchIsAtomic(t *testing.T) {
	pg := storetest.New(t)
	ctx := context.Background()
	now := time.Now()
	// node row references a gateway that does not exist -> FK violation -> whole batch rolls back, including metrics.
	err := pg.WriteBatch(ctx, store.Batch{
		Metrics:    []store.MetricRow{{TS: now, GatewayID: 9, NodeID: 1, Name: "x", Value: 1}},
		Heartbeats: []store.NodeHeartbeat{hb(9, 1, 1, 0, false, 1, now)},
	})
	if err == nil {
		t.Fatal("expected FK failure")
	}
	var n int
	pg.Pool.QueryRow(ctx, `SELECT count(*) FROM metrics`).Scan(&n)
	if n != 0 {
		t.Fatalf("partial batch committed (%d metrics rows)", n)
	}
}

func TestAlertsRaiseOnceAndClear(t *testing.T) {
	pg := storetest.New(t)
	ctx := context.Background()
	if fresh, _ := pg.RaiseAlert(ctx, "node_silent", 1, 1, "critical", "a"); !fresh {
		t.Fatal("first raise must be new")
	}
	if fresh, _ := pg.RaiseAlert(ctx, "node_silent", 1, 1, "critical", "b"); fresh {
		t.Fatal("second raise must be deduplicated")
	}
	open, _ := pg.ListAlerts(ctx, true, 10)
	if len(open) != 1 || open[0].Message != "b" {
		t.Fatalf("%+v", open)
	}
	pg.ClearAlert(ctx, "node_silent", 1, 1)
	if open, _ = pg.ListAlerts(ctx, true, 10); len(open) != 0 {
		t.Fatal("not cleared")
	}
	if fresh, _ := pg.RaiseAlert(ctx, "node_silent", 1, 1, "critical", "again"); !fresh {
		t.Fatal("after clear a new alert must open")
	}
}

func TestRolloutPlanning(t *testing.T) {
	pg := storetest.New(t)
	ctx := context.Background()
	now := time.Now()
	for gw := int16(1); gw <= 3; gw++ {
		pg.WriteBatch(ctx, store.Batch{Gateways: []store.GatewayUpdate{{ID: gw, TS: now}}, Heartbeats: []store.NodeHeartbeat{hb(gw, 2, 1, 0, false, 1, now)}})
	}
	pg.Pool.Exec(ctx, `UPDATE nodes SET last_seen = now() - interval '1 hour' WHERE gateway_id=3`) // gateway 3 went dark
	img, err := pg.InsertImage(ctx, 2, 2, []byte("a"), []byte("b"), "")
	if err != nil {
		t.Fatal(err)
	}
	canary := int16(2)
	id, err := pg.CreateRollout(ctx, img, &canary, 30000, 3000, 10*time.Second)
	if err != nil {
		t.Fatal(err)
	}
	r, _ := pg.GetRollout(ctx, id)
	if len(r.Steps) != 3 || r.Steps[0].GatewayID != 2 || r.Steps[0].Phase != "canary" || r.Steps[1].GatewayID != 1 || r.Steps[2].State != "skipped" {
		t.Fatalf("plan: %+v", r.Steps)
	}
	if _, err := pg.CreateRollout(ctx, img, nil, 30000, 3000, 10*time.Second); err == nil {
		t.Fatal("second concurrent rollout for the same node type must be refused")
	}
	if _, err := pg.InsertImage(ctx, 2, 2, nil, nil, ""); err == nil {
		t.Fatal("duplicate (node, version) must be refused")
	}
}

func TestCursorsAreCommittedAtomicallyWithData(t *testing.T) {
	pg := storetest.New(t)
	ctx := context.Background()
	now := time.Now()
	if err := pg.WriteBatch(ctx, store.Batch{Gateways: []store.GatewayUpdate{{ID: 1, TS: now}}, Cursors: map[int16]uint32{1: 77}}); err != nil {
		t.Fatal(err)
	}
	// A failing batch (FK violation) must not advance the cursor.
	err := pg.WriteBatch(ctx, store.Batch{Cursors: map[int16]uint32{1: 99}, Heartbeats: []store.NodeHeartbeat{hb(9, 1, 1, 0, false, 1, now)}})
	if err == nil {
		t.Fatal("expected failure")
	}
	c, _ := pg.LoadCursors(ctx)
	if c[1] != 77 {
		t.Fatalf("cursor advanced by a failed batch: %v", c)
	}
}
