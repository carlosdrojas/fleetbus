// Package metrics is a tiny Prometheus-text-format registry (counters and gauges), so the services expose /metrics
// without pulling in a client library.
package metrics

import (
	"fmt"
	"io"
	"net/http"
	"sort"
	"sync"
	"sync/atomic"
)

type Counter struct{ v atomic.Int64 }

func (c *Counter) Inc()         { c.v.Add(1) }
func (c *Counter) Add(n int64)  { c.v.Add(n) }
func (c *Counter) Value() int64 { return c.v.Load() }

type Gauge struct{ v atomic.Int64 }

func (g *Gauge) Set(n int64)  { g.v.Store(n) }
func (g *Gauge) Value() int64 { return g.v.Load() }

type Registry struct {
	mu       sync.Mutex
	counters map[string]*Counter
	gauges   map[string]*Gauge
	help     map[string]string
	funcs    map[string]func() float64
}

func New() *Registry {
	return &Registry{counters: map[string]*Counter{}, gauges: map[string]*Gauge{}, help: map[string]string{}, funcs: map[string]func() float64{}}
}

func (r *Registry) Counter(name, help string) *Counter {
	r.mu.Lock()
	defer r.mu.Unlock()
	if c, ok := r.counters[name]; ok {
		return c
	}
	c := &Counter{}
	r.counters[name], r.help[name] = c, help
	return c
}

func (r *Registry) Gauge(name, help string) *Gauge {
	r.mu.Lock()
	defer r.mu.Unlock()
	if g, ok := r.gauges[name]; ok {
		return g
	}
	g := &Gauge{}
	r.gauges[name], r.help[name] = g, help
	return g
}

// GaugeFunc registers a gauge computed at scrape time.
func (r *Registry) GaugeFunc(name, help string, f func() float64) {
	r.mu.Lock()
	r.funcs[name], r.help[name] = f, help
	r.mu.Unlock()
}

func (r *Registry) Write(w io.Writer) {
	r.mu.Lock()
	defer r.mu.Unlock()
	var names []string
	for n := range r.counters {
		names = append(names, n)
	}
	for n := range r.gauges {
		names = append(names, n)
	}
	for n := range r.funcs {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		typ, val := "gauge", ""
		if c, ok := r.counters[n]; ok {
			typ, val = "counter", fmt.Sprint(c.Value())
		} else if g, ok := r.gauges[n]; ok {
			val = fmt.Sprint(g.Value())
		} else {
			val = fmt.Sprint(r.funcs[n]())
		}
		fmt.Fprintf(w, "# HELP %s %s\n# TYPE %s %s\n%s %s\n", n, r.help[n], n, typ, n, val)
	}
}

func (r *Registry) Handler() http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "text/plain; version=0.0.4")
		r.Write(w)
	})
}
