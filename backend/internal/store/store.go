// Package store is the Postgres layer: schema migrations, the batch writer used by ingest, and the queries used by the
// API, alert engine, OTA driver and rollout orchestrator.
package store

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"sync"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

const NotifyChannel = "fleetbus"

type PG struct {
	Pool *pgxpool.Pool
	mu   sync.Mutex
	part map[string]bool
}

func Open(ctx context.Context, dsn string) (*PG, error) {
	pool, err := pgxpool.New(ctx, dsn)
	if err != nil {
		return nil, err
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, err
	}
	return &PG{Pool: pool, part: map[string]bool{}}, nil
}

func (p *PG) Close() { p.Pool.Close() }

func (p *PG) Migrate(ctx context.Context) error { return Migrate(ctx, p.Pool) }

func (p *PG) Notify(ctx context.Context, kind string) {
	p.Pool.Exec(ctx, `SELECT pg_notify($1, $2)`, NotifyChannel, kind)
}

// effectiveTS trusts the gateway's frame time unless it is wildly off the receive time (clock skew), and never
// lets a frame look newer than when we received it.
func EffectiveTS(frame, recv time.Time) time.Time {
	d := recv.Sub(frame)
	if d > 10*time.Minute || d < -10*time.Minute {
		return recv
	}
	if frame.After(recv) {
		return recv
	}
	return frame
}

func (p *PG) ensurePartitions(ctx context.Context, rows []TelRow) error {
	need := map[string]time.Time{}
	for _, r := range rows {
		d := r.TS.UTC().Truncate(24 * time.Hour)
		need[d.Format("20060102")] = d
	}
	for k, d := range need {
		p.mu.Lock()
		ok := p.part[k]
		p.mu.Unlock()
		if ok {
			continue
		}
		if _, err := p.Pool.Exec(ctx, `SELECT ensure_telemetry_partition($1::date)`, d); err != nil {
			return err
		}
		p.mu.Lock()
		p.part[k] = true
		p.mu.Unlock()
	}
	return nil
}

// WriteBatch commits one ingest batch atomically. Rows are COPYed; node state transitions become node_events.
func (p *PG) WriteBatch(ctx context.Context, b Batch) error {
	if err := p.ensurePartitions(ctx, b.Telemetry); err != nil {
		return err
	}
	tx, err := p.Pool.Begin(ctx)
	if err != nil {
		return err
	}
	defer tx.Rollback(ctx)

	for gw, seq := range b.Cursors {
		if _, err := tx.Exec(ctx, `INSERT INTO gateway_cursors(gateway_id, committed_seq) VALUES ($1,$2)
			ON CONFLICT (gateway_id) DO UPDATE SET committed_seq=EXCLUDED.committed_seq, updated_at=now()`, gw, int64(seq)); err != nil {
			return fmt.Errorf("cursor: %w", err)
		}
	}
	for _, g := range b.Gateways {
		if _, err := tx.Exec(ctx, `INSERT INTO gateways(id, addr, simulated, first_seen, last_seen) VALUES ($1,$2,$3,$4,$4)
			ON CONFLICT (id) DO UPDATE SET addr=EXCLUDED.addr, simulated=EXCLUDED.simulated, last_seen=GREATEST(gateways.last_seen, EXCLUDED.last_seen)`,
			g.ID, g.Addr, g.Simulated, g.TS); err != nil {
			return fmt.Errorf("gateway upsert: %w", err)
		}
		if g.HasStats {
			if _, err := tx.Exec(ctx, `UPDATE gateways SET uptime_s=$2, unacked=$3, spooled=$4, resent=$5 WHERE id=$1`, g.ID, g.UptimeS, g.Unacked, g.Spooled, g.Resent); err != nil {
				return err
			}
		}
	}

	if len(b.Telemetry) > 0 {
		rows := make([][]any, len(b.Telemetry))
		for i, r := range b.Telemetry {
			rows[i] = []any{r.TS, r.GatewayID, r.NodeID, r.CanID, r.Data}
		}
		if _, err := tx.CopyFrom(ctx, pgx.Identifier{"telemetry"}, []string{"ts", "gateway_id", "node_id", "can_id", "data"}, pgx.CopyFromRows(rows)); err != nil {
			return fmt.Errorf("copy telemetry: %w", err)
		}
	}
	if len(b.Metrics) > 0 {
		rows := make([][]any, len(b.Metrics))
		for i, r := range b.Metrics {
			rows[i] = []any{r.TS, r.GatewayID, r.NodeID, r.Name, r.Value}
		}
		if _, err := tx.CopyFrom(ctx, pgx.Identifier{"metrics"}, []string{"ts", "gateway_id", "node_id", "name", "value"}, pgx.CopyFromRows(rows)); err != nil {
			return fmt.Errorf("copy metrics: %w", err)
		}
	}
	if len(b.Faults) > 0 {
		rows := make([][]any, len(b.Faults))
		for i, r := range b.Faults {
			rows[i] = []any{r.TS, r.GatewayID, r.NodeID, r.Code, r.Severity}
		}
		if _, err := tx.CopyFrom(ctx, pgx.Identifier{"faults"}, []string{"ts", "gateway_id", "node_id", "code", "severity"}, pgx.CopyFromRows(rows)); err != nil {
			return fmt.Errorf("copy faults: %w", err)
		}
	}

	// Heartbeats arrive at ~10 Hz per node. Doing a read+write per heartbeat made a 500-packet batch take seconds, so
	// group them: read each node once, walk its heartbeats in order emitting events only where state changes, write
	// the final state once.
	type key struct{ gw, node int16 }
	order := []key{}
	byNode := map[key][]NodeHeartbeat{}
	for _, h := range b.Heartbeats {
		k := key{h.GatewayID, h.NodeID}
		if _, ok := byNode[k]; !ok {
			order = append(order, k)
		}
		byNode[k] = append(byNode[k], h)
	}
	for _, k := range order {
		hbs := byNode[k]
		var cur Node
		exists := true
		err := tx.QueryRow(ctx, `SELECT fw_version, slot, trial, state, last_seen FROM nodes WHERE gateway_id=$1 AND node_id=$2 FOR UPDATE`, k.gw, k.node).
			Scan(&cur.FwVersion, &cur.Slot, &cur.Trial, &cur.State, &cur.LastSeen)
		if errors.Is(err, pgx.ErrNoRows) {
			exists = false
		} else if err != nil {
			return err
		}
		name := NodeNames[k.node]
		if name == "" {
			name = fmt.Sprintf("node%d", k.node)
		}
		changed := false
		for _, h := range hbs {
			if !exists {
				if _, err := tx.Exec(ctx, `INSERT INTO nodes(gateway_id,node_id,name,fw_version,slot,trial,state,first_seen,last_seen) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$8)`,
					h.GatewayID, h.NodeID, name, h.FwVersion, h.Slot, h.Trial, h.State, h.TS); err != nil {
					return fmt.Errorf("node insert: %w", err)
				}
				addEvent(ctx, tx, h, "first_seen", map[string]any{"fw": h.FwVersion, "slot": h.Slot})
				cur = Node{FwVersion: h.FwVersion, Slot: h.Slot, Trial: h.Trial, State: h.State, LastSeen: h.TS}
				exists = true
				continue
			}
			if h.TS.Before(cur.LastSeen) { // late (replayed) heartbeat: it must not roll the node's current state back
				continue
			}
			if cur.State != h.State {
				addEvent(ctx, tx, h, "state", map[string]any{"from": cur.State, "to": h.State})
			}
			if cur.FwVersion != h.FwVersion {
				addEvent(ctx, tx, h, "version", map[string]any{"from": cur.FwVersion, "to": h.FwVersion})
			}
			if cur.Slot != h.Slot {
				addEvent(ctx, tx, h, "slot", map[string]any{"from": cur.Slot, "to": h.Slot})
			}
			if cur.Trial != h.Trial {
				addEvent(ctx, tx, h, "trial", map[string]any{"from": cur.Trial, "to": h.Trial})
			}
			cur = Node{FwVersion: h.FwVersion, Slot: h.Slot, Trial: h.Trial, State: h.State, LastSeen: h.TS}
			changed = true
		}
		if changed {
			if _, err := tx.Exec(ctx, `UPDATE nodes SET fw_version=$3, slot=$4, trial=$5, state=$6, last_seen=$7 WHERE gateway_id=$1 AND node_id=$2`,
				k.gw, k.node, cur.FwVersion, cur.Slot, cur.Trial, cur.State, cur.LastSeen); err != nil {
				return err
			}
		}
	}

	for _, s := range b.OtaStatuses {
		// 0 accepted · 1 progress · 2 transferred · 3 failed · 4 verify failed (see protocol/spec.md)
		state := "active"
		switch s.Status {
		case 2:
			state = "transferred"
		case 3:
			state = "failed"
		case 4:
			state = "verify_failed"
		}
		if _, err := tx.Exec(ctx, `UPDATE ota_transfers SET state=$3, next_offset=GREATEST(next_offset,$4), detail=CASE WHEN $5<>0 THEN 'gateway detail '||$5 ELSE detail END, updated_at=now()
			WHERE gateway_id=$1 AND node_id=$2 AND state IN ('queued','active')`, s.GatewayID, s.NodeID, state, s.NextOffset, s.Detail); err != nil {
			return err
		}
	}
	if len(b.Heartbeats) > 0 || len(b.Gateways) > 0 {
		tx.Exec(ctx, `SELECT pg_notify($1,'nodes')`, NotifyChannel)
	}
	if len(b.Faults) > 0 {
		tx.Exec(ctx, `SELECT pg_notify($1,'faults')`, NotifyChannel)
	}
	if len(b.OtaStatuses) > 0 {
		tx.Exec(ctx, `SELECT pg_notify($1,'ota')`, NotifyChannel)
	}
	return tx.Commit(ctx)
}

func addEvent(ctx context.Context, tx pgx.Tx, h NodeHeartbeat, kind string, detail map[string]any) {
	j, _ := json.Marshal(detail)
	tx.Exec(ctx, `INSERT INTO node_events(ts, gateway_id, node_id, kind, detail) VALUES ($1,$2,$3,$4,$5)`, h.TS, h.GatewayID, h.NodeID, kind, j)
}

// ---- reads -------------------------------------------------------------------------------------------------

// LoadCursors returns the committed FBP sequence number per gateway.
func (p *PG) LoadCursors(ctx context.Context) (map[int16]uint32, error) {
	rows, err := p.Pool.Query(ctx, `SELECT gateway_id, committed_seq FROM gateway_cursors`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := map[int16]uint32{}
	for rows.Next() {
		var g int16
		var c int64
		if err := rows.Scan(&g, &c); err != nil {
			return nil, err
		}
		out[g] = uint32(c)
	}
	return out, rows.Err()
}

func (p *PG) ListGateways(ctx context.Context) ([]Gateway, error) {
	rows, err := p.Pool.Query(ctx, `SELECT id, COALESCE(addr,''), simulated, first_seen, last_seen, uptime_s, unacked, spooled, resent FROM gateways ORDER BY id`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Gateway
	for rows.Next() {
		var g Gateway
		if err := rows.Scan(&g.ID, &g.Addr, &g.Simulated, &g.FirstSeen, &g.LastSeen, &g.UptimeS, &g.Unacked, &g.Spooled, &g.Resent); err != nil {
			return nil, err
		}
		out = append(out, g)
	}
	return out, rows.Err()
}

func (p *PG) ListNodes(ctx context.Context) ([]Node, error) {
	rows, err := p.Pool.Query(ctx, `SELECT gateway_id, node_id, name, fw_version, slot, trial, state, first_seen, last_seen FROM nodes ORDER BY gateway_id, node_id`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Node
	for rows.Next() {
		var n Node
		if err := rows.Scan(&n.GatewayID, &n.NodeID, &n.Name, &n.FwVersion, &n.Slot, &n.Trial, &n.State, &n.FirstSeen, &n.LastSeen); err != nil {
			return nil, err
		}
		out = append(out, n)
	}
	return out, rows.Err()
}

func (p *PG) GetNode(ctx context.Context, gw, node int16) (*Node, error) {
	var n Node
	err := p.Pool.QueryRow(ctx, `SELECT gateway_id, node_id, name, fw_version, slot, trial, state, first_seen, last_seen FROM nodes WHERE gateway_id=$1 AND node_id=$2`, gw, node).
		Scan(&n.GatewayID, &n.NodeID, &n.Name, &n.FwVersion, &n.Slot, &n.Trial, &n.State, &n.FirstSeen, &n.LastSeen)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, nil
	}
	return &n, err
}

func (p *PG) Metrics(ctx context.Context, gw, node int16, name string, since time.Time, limit int) ([]MetricRow, error) {
	rows, err := p.Pool.Query(ctx, `SELECT ts, gateway_id, node_id, name, value FROM (
		SELECT ts, gateway_id, node_id, name, value FROM metrics WHERE gateway_id=$1 AND node_id=$2 AND name=$3 AND ts>=$4 ORDER BY ts DESC LIMIT $5) q ORDER BY ts`, gw, node, name, since, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []MetricRow
	for rows.Next() {
		var m MetricRow
		if err := rows.Scan(&m.TS, &m.GatewayID, &m.NodeID, &m.Name, &m.Value); err != nil {
			return nil, err
		}
		out = append(out, m)
	}
	return out, rows.Err()
}

func (p *PG) RecentFaults(ctx context.Context, limit int) ([]FaultRow, error) {
	rows, err := p.Pool.Query(ctx, `SELECT id, ts, gateway_id, node_id, code, severity FROM faults ORDER BY ts DESC, id DESC LIMIT $1`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []FaultRow
	for rows.Next() {
		var f FaultRow
		if err := rows.Scan(&f.ID, &f.TS, &f.GatewayID, &f.NodeID, &f.Code, &f.Severity); err != nil {
			return nil, err
		}
		out = append(out, f)
	}
	return out, rows.Err()
}

func (p *PG) CountFaultsSince(ctx context.Context, gw, node int16, since time.Time) (int, error) {
	var n int
	err := p.Pool.QueryRow(ctx, `SELECT count(*) FROM faults WHERE gateway_id=$1 AND node_id=$2 AND ts>=$3`, gw, node, since).Scan(&n)
	return n, err
}

func (p *PG) NodeEvents(ctx context.Context, limit int) ([]NodeEvent, error) {
	rows, err := p.Pool.Query(ctx, `SELECT id, ts, gateway_id, node_id, kind, detail FROM node_events ORDER BY ts DESC, id DESC LIMIT $1`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []NodeEvent
	for rows.Next() {
		var e NodeEvent
		var raw []byte
		if err := rows.Scan(&e.ID, &e.TS, &e.GatewayID, &e.NodeID, &e.Kind, &raw); err != nil {
			return nil, err
		}
		json.Unmarshal(raw, &e.Detail)
		out = append(out, e)
	}
	return out, rows.Err()
}

// ---- alerts ------------------------------------------------------------------------------------------------

// RaiseAlert opens an alert if none is open for (rule, gateway, node). Returns true if it was newly raised.
func (p *PG) RaiseAlert(ctx context.Context, rule string, gw, node int16, severity, msg string) (bool, error) {
	var id int64
	err := p.Pool.QueryRow(ctx, `INSERT INTO alerts(rule, gateway_id, node_id, severity, message) VALUES ($1,$2,$3,$4,$5)
		ON CONFLICT (rule, gateway_id, node_id) WHERE cleared_at IS NULL DO NOTHING RETURNING id`, rule, gw, node, severity, msg).Scan(&id)
	if errors.Is(err, pgx.ErrNoRows) {
		p.Pool.Exec(ctx, `UPDATE alerts SET message=$4 WHERE rule=$1 AND gateway_id=$2 AND node_id=$3 AND cleared_at IS NULL`, rule, gw, node, msg)
		return false, nil
	}
	if err != nil {
		return false, err
	}
	p.Notify(ctx, "alerts")
	return true, nil
}

func (p *PG) ClearAlert(ctx context.Context, rule string, gw, node int16) error {
	tag, err := p.Pool.Exec(ctx, `UPDATE alerts SET cleared_at=now() WHERE rule=$1 AND gateway_id=$2 AND node_id=$3 AND cleared_at IS NULL`, rule, gw, node)
	if err == nil && tag.RowsAffected() > 0 {
		p.Notify(ctx, "alerts")
	}
	return err
}

func (p *PG) ListAlerts(ctx context.Context, openOnly bool, limit int) ([]Alert, error) {
	q := `SELECT id, rule, gateway_id, node_id, severity, message, raised_at, cleared_at FROM alerts `
	if openOnly {
		q += `WHERE cleared_at IS NULL `
	}
	rows, err := p.Pool.Query(ctx, q+`ORDER BY raised_at DESC, id DESC LIMIT $1`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Alert
	for rows.Next() {
		var a Alert
		if err := rows.Scan(&a.ID, &a.Rule, &a.GatewayID, &a.NodeID, &a.Severity, &a.Message, &a.RaisedAt, &a.ClearedAt); err != nil {
			return nil, err
		}
		out = append(out, a)
	}
	return out, rows.Err()
}
