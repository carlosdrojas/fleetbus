// Package alerts evaluates fleet health rules against the nodes/gateways tables and opens/clears alerts.
// Liveness uses last_seen derived from event time, so a gateway replaying a backlog after an outage cannot make a
// silent node look alive.
package alerts

import (
	"context"
	"fmt"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/canmsg"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type Repo interface {
	ListNodes(ctx context.Context) ([]store.Node, error)
	ListGateways(ctx context.Context) ([]store.Gateway, error)
	CountFaultsSince(ctx context.Context, gw, node int16, since time.Time) (int, error)
	RaiseAlert(ctx context.Context, rule string, gw, node int16, severity, msg string) (bool, error)
	ClearAlert(ctx context.Context, rule string, gw, node int16) error
}

type Config struct {
	NodeSilentAfter    time.Duration // default 2 s (20 missed 10 Hz heartbeats; TARGET)
	GatewaySilentAfter time.Duration // default 5 s
	FaultSpikeCount    int           // default 3 faults ...
	FaultSpikeWindow   time.Duration // ... within 10 s
}

func (c *Config) defaults() {
	if c.NodeSilentAfter == 0 {
		c.NodeSilentAfter = 2 * time.Second
	}
	if c.GatewaySilentAfter == 0 {
		c.GatewaySilentAfter = 5 * time.Second
	}
	if c.FaultSpikeCount == 0 {
		c.FaultSpikeCount = 3
	}
	if c.FaultSpikeWindow == 0 {
		c.FaultSpikeWindow = 10 * time.Second
	}
}

type Engine struct {
	Repo Repo
	Cfg  Config
	Now  func() time.Time
	Logf func(string, ...any)
}

func (e *Engine) set(ctx context.Context, active bool, rule string, gw, node int16, sev, msg string) {
	if active {
		if fresh, err := e.Repo.RaiseAlert(ctx, rule, gw, node, sev, msg); err == nil && fresh && e.Logf != nil {
			e.Logf("ALERT %s gw=%d node=%d: %s", rule, gw, node, msg)
		}
		return
	}
	e.Repo.ClearAlert(ctx, rule, gw, node)
}

// Evaluate runs every rule once.
func (e *Engine) Evaluate(ctx context.Context) error {
	e.Cfg.defaults()
	now := e.Now()
	gws, err := e.Repo.ListGateways(ctx)
	if err != nil {
		return err
	}
	for _, g := range gws {
		age := now.Sub(g.LastSeen)
		e.set(ctx, age > e.Cfg.GatewaySilentAfter, "gateway_silent", g.ID, 0, "critical",
			fmt.Sprintf("gateway %d has not reported for %s", g.ID, age.Round(100*time.Millisecond)))
	}
	nodes, err := e.Repo.ListNodes(ctx)
	if err != nil {
		return err
	}
	gwSilent := map[int16]bool{}
	for _, g := range gws {
		gwSilent[g.ID] = now.Sub(g.LastSeen) > e.Cfg.GatewaySilentAfter
	}
	for _, n := range nodes {
		age := now.Sub(n.LastSeen)
		silent := age > e.Cfg.NodeSilentAfter
		// If the whole gateway is silent, the gateway alert already says so; do not bury it in per-node noise.
		e.set(ctx, silent && !gwSilent[n.GatewayID], "node_silent", n.GatewayID, n.NodeID, "critical",
			fmt.Sprintf("%s node (gateway %d) silent for %s", n.Name, n.GatewayID, age.Round(100*time.Millisecond)))
		st := int(n.State)
		e.set(ctx, !silent && (st == canmsg.StateFault || st == canmsg.StateSafe), "node_fault", n.GatewayID, n.NodeID, "critical",
			fmt.Sprintf("%s node (gateway %d) is in %s", n.Name, n.GatewayID, canmsg.StateName(st)))
		e.set(ctx, !silent && st == canmsg.StateLimpHome, "node_limp_home", n.GatewayID, n.NodeID, "warning",
			fmt.Sprintf("%s node (gateway %d) is in limp-home", n.Name, n.GatewayID))
		c, err := e.Repo.CountFaultsSince(ctx, n.GatewayID, n.NodeID, now.Add(-e.Cfg.FaultSpikeWindow))
		if err == nil {
			e.set(ctx, c >= e.Cfg.FaultSpikeCount, "fault_spike", n.GatewayID, n.NodeID, "warning",
				fmt.Sprintf("%s node (gateway %d): %d fault events in %s", n.Name, n.GatewayID, c, e.Cfg.FaultSpikeWindow))
		}
	}
	return nil
}

// Run evaluates every interval until ctx ends.
func (e *Engine) Run(ctx context.Context, every time.Duration) {
	t := time.NewTicker(every)
	defer t.Stop()
	for {
		if err := e.Evaluate(ctx); err != nil && ctx.Err() == nil && e.Logf != nil {
			e.Logf("alerts: %v", err)
		}
		select {
		case <-t.C:
		case <-ctx.Done():
			return
		}
	}
}
