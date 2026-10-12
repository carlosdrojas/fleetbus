// Package api is the HTTP face of FleetBus: REST for the dashboard and fbctl, Server-Sent Events for live updates,
// and Prometheus /metrics. It owns no state; everything comes from Postgres.
package api

import (
	"context"
	"crypto/ed25519"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"sync"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/canmsg"
	"github.com/carlosdrojas/fleetbus/backend/internal/imagefmt"
	"github.com/carlosdrojas/fleetbus/backend/internal/metrics"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

type Repo interface {
	ListGateways(ctx context.Context) ([]store.Gateway, error)
	ListNodes(ctx context.Context) ([]store.Node, error)
	Metrics(ctx context.Context, gw, node int16, name string, since time.Time, limit int) ([]store.MetricRow, error)
	RecentFaults(ctx context.Context, limit int) ([]store.FaultRow, error)
	NodeEvents(ctx context.Context, limit int) ([]store.NodeEvent, error)
	ListAlerts(ctx context.Context, openOnly bool, limit int) ([]store.Alert, error)
	InsertImage(ctx context.Context, node int16, version int32, blobA, blobB []byte, notes string) (int32, error)
	ListImages(ctx context.Context) ([]store.Image, error)
	GetImage(ctx context.Context, id int32) (*store.Image, error)
	CreateRollout(ctx context.Context, imageID int32, canaryGW *int16, healthWindowMS, soakMS int32, onlineWithin time.Duration) (int32, error)
	ListRollouts(ctx context.Context, limit int) ([]store.Rollout, error)
	GetRollout(ctx context.Context, id int32) (*store.Rollout, error)
	AbortRollout(ctx context.Context, id int32, why string) error
	TransferForStep(ctx context.Context, stepID int32) (*store.Transfer, error)
}

type Server struct {
	Repo      Repo
	Hub       *Hub
	Reg       *metrics.Registry
	PublicKey ed25519.PublicKey // when set, uploaded images must verify against it
	Static    string            // directory with the built dashboard, optional
	Now       func() time.Time
	// Thresholds for the "health" field of /api/fleet
	SilentAfter time.Duration

	reqs *metrics.Counter
}

func (s *Server) Handler() http.Handler {
	if s.Now == nil {
		s.Now = time.Now
	}
	if s.SilentAfter == 0 {
		s.SilentAfter = 2 * time.Second
	}
	if s.Reg == nil {
		s.Reg = metrics.New()
	}
	s.reqs = s.Reg.Counter("fleetbus_api_requests_total", "HTTP requests served")
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { io.WriteString(w, "ok\n") })
	mux.Handle("GET /metrics", s.Reg.Handler())
	mux.HandleFunc("GET /api/fleet", s.fleet)
	mux.HandleFunc("GET /api/nodes/{gw}/{node}/metrics", s.nodeMetrics)
	mux.HandleFunc("GET /api/faults", s.faults)
	mux.HandleFunc("GET /api/events", s.events)
	mux.HandleFunc("GET /api/alerts", s.alerts)
	mux.HandleFunc("GET /api/images", s.images)
	mux.HandleFunc("POST /api/images", s.uploadImage)
	mux.HandleFunc("GET /api/rollouts", s.rollouts)
	mux.HandleFunc("POST /api/rollouts", s.startRollout)
	mux.HandleFunc("GET /api/rollouts/{id}", s.rollout)
	mux.HandleFunc("POST /api/rollouts/{id}/abort", s.abortRollout)
	mux.HandleFunc("GET /api/stream", s.stream)
	if s.Static != "" {
		mux.Handle("/", http.FileServer(http.Dir(s.Static)))
	}
	return s.wrap(mux)
}

func (s *Server) wrap(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		s.reqs.Inc()
		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		if r.Method == http.MethodOptions {
			return
		}
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func fail(w http.ResponseWriter, code int, format string, a ...any) {
	writeJSON(w, code, map[string]string{"error": fmt.Sprintf(format, a...)})
}

func (s *Server) dbErr(w http.ResponseWriter, err error) {
	fail(w, http.StatusInternalServerError, "database error: %v", err)
}

// ---- fleet -------------------------------------------------------------------------------------------------

type nodeView struct {
	store.Node
	StateName string `json:"state_name"`
	SlotName  string `json:"slot_name"`
	AgeMS     int64  `json:"age_ms"`
	Health    string `json:"health"` // ok | warning | critical | silent
	Simulated bool   `json:"simulated"`
}

type gatewayView struct {
	store.Gateway
	AgeMS  int64      `json:"age_ms"`
	Health string     `json:"health"`
	Nodes  []nodeView `json:"nodes"`
}

func (s *Server) fleet(w http.ResponseWriter, r *http.Request) {
	gws, err := s.Repo.ListGateways(r.Context())
	if err != nil {
		s.dbErr(w, err)
		return
	}
	nodes, err := s.Repo.ListNodes(r.Context())
	if err != nil {
		s.dbErr(w, err)
		return
	}
	now := s.Now()
	out := make([]gatewayView, 0, len(gws))
	idx := map[int16]int{}
	for _, g := range gws {
		age := now.Sub(g.LastSeen)
		h := "ok"
		if age > 2*s.SilentAfter+time.Second {
			h = "silent"
		}
		idx[g.ID] = len(out)
		out = append(out, gatewayView{Gateway: g, AgeMS: age.Milliseconds(), Health: h, Nodes: []nodeView{}})
	}
	for _, n := range nodes {
		i, ok := idx[n.GatewayID]
		if !ok {
			continue
		}
		age := now.Sub(n.LastSeen)
		h := "ok"
		switch {
		case age > s.SilentAfter:
			h = "silent"
		case int(n.State) == canmsg.StateFault || int(n.State) == canmsg.StateSafe:
			h = "critical"
		case int(n.State) == canmsg.StateWarning || int(n.State) == canmsg.StateLimpHome || n.Trial:
			h = "warning"
		}
		out[i].Nodes = append(out[i].Nodes, nodeView{Node: n, StateName: canmsg.StateName(int(n.State)), SlotName: string(rune('A' + n.Slot)), AgeMS: age.Milliseconds(), Health: h, Simulated: out[i].Simulated})
	}
	writeJSON(w, 200, map[string]any{"now": now, "gateways": out})
}

func (s *Server) nodeMetrics(w http.ResponseWriter, r *http.Request) {
	gw, e1 := strconv.Atoi(r.PathValue("gw"))
	node, e2 := strconv.Atoi(r.PathValue("node"))
	name := r.URL.Query().Get("name")
	if e1 != nil || e2 != nil || name == "" {
		fail(w, 400, "need numeric gw/node and ?name=")
		return
	}
	window := 2 * time.Minute
	if v := r.URL.Query().Get("window"); v != "" {
		if d, err := time.ParseDuration(v); err == nil && d > 0 && d <= 24*time.Hour {
			window = d
		}
	}
	rows, err := s.Repo.Metrics(r.Context(), int16(gw), int16(node), name, s.Now().Add(-window), 2000)
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if rows == nil {
		rows = []store.MetricRow{}
	}
	writeJSON(w, 200, rows)
}

func (s *Server) faults(w http.ResponseWriter, r *http.Request) {
	rows, err := s.Repo.RecentFaults(r.Context(), 100)
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if rows == nil {
		rows = []store.FaultRow{}
	}
	writeJSON(w, 200, rows)
}

func (s *Server) events(w http.ResponseWriter, r *http.Request) {
	rows, err := s.Repo.NodeEvents(r.Context(), 100)
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if rows == nil {
		rows = []store.NodeEvent{}
	}
	writeJSON(w, 200, rows)
}

func (s *Server) alerts(w http.ResponseWriter, r *http.Request) {
	rows, err := s.Repo.ListAlerts(r.Context(), r.URL.Query().Get("state") != "all", 100)
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if rows == nil {
		rows = []store.Alert{}
	}
	writeJSON(w, 200, rows)
}

// ---- images ------------------------------------------------------------------------------------------------

func (s *Server) images(w http.ResponseWriter, r *http.Request) {
	rows, err := s.Repo.ListImages(r.Context())
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if rows == nil {
		rows = []store.Image{}
	}
	writeJSON(w, 200, rows)
}

func readUpload(r *http.Request, field string) ([]byte, error) {
	f, _, err := r.FormFile(field)
	if err != nil {
		return nil, fmt.Errorf("missing file %q", field)
	}
	defer f.Close()
	return io.ReadAll(io.LimitReader(f, imagefmt.SlotSize+1))
}

// uploadImage takes the two signed builds of one firmware version (linked for slot A and for slot B).
func (s *Server) uploadImage(w http.ResponseWriter, r *http.Request) {
	r.Body = http.MaxBytesReader(w, r.Body, 4<<20)
	if err := r.ParseMultipartForm(4 << 20); err != nil {
		fail(w, 400, "bad multipart form: %v", err)
		return
	}
	a, err := readUpload(r, "slot_a")
	if err != nil {
		fail(w, 400, "%v", err)
		return
	}
	b, err := readUpload(r, "slot_b")
	if err != nil {
		fail(w, 400, "%v", err)
		return
	}
	check := func(blob []byte, slot uint8) (imagefmt.Header, error) {
		var h imagefmt.Header
		var err error
		if s.PublicKey != nil {
			h, err = imagefmt.Verify(blob, s.PublicKey)
		} else {
			h, err = imagefmt.Parse(blob)
		}
		if err == nil && h.Slot != slot {
			err = fmt.Errorf("image is linked for slot %c, expected %c", 'A'+h.Slot, 'A'+slot)
		}
		return h, err
	}
	ha, err := check(a, 0)
	if err != nil {
		fail(w, 422, "slot_a: %v", err)
		return
	}
	hb, err := check(b, 1)
	if err != nil {
		fail(w, 422, "slot_b: %v", err)
		return
	}
	if ha.Node != hb.Node || ha.Version != hb.Version {
		fail(w, 422, "slot A and B builds disagree (node %d v%d vs node %d v%d)", ha.Node, ha.Version, hb.Node, hb.Version)
		return
	}
	id, err := s.Repo.InsertImage(r.Context(), int16(ha.Node), int32(ha.Version), a, b, r.FormValue("notes"))
	if err != nil {
		fail(w, 409, "could not store image (duplicate version?): %v", err)
		return
	}
	writeJSON(w, 201, map[string]any{"id": id, "node_id": ha.Node, "version": ha.Version, "verified": s.PublicKey != nil})
}

// ---- rollouts ----------------------------------------------------------------------------------------------

func (s *Server) rollouts(w http.ResponseWriter, r *http.Request) {
	rows, err := s.Repo.ListRollouts(r.Context(), 50)
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if rows == nil {
		rows = []store.Rollout{}
	}
	writeJSON(w, 200, rows)
}

func (s *Server) startRollout(w http.ResponseWriter, r *http.Request) {
	var in struct {
		ImageID        int32  `json:"image_id"`
		CanaryGateway  *int16 `json:"canary_gateway"`
		HealthWindowMS int32  `json:"health_window_ms"`
		SoakMS         int32  `json:"soak_ms"`
	}
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<16)).Decode(&in); err != nil || in.ImageID == 0 {
		fail(w, 400, "need JSON {\"image_id\": N}")
		return
	}
	if in.HealthWindowMS == 0 {
		in.HealthWindowMS = 30000
	}
	if in.SoakMS == 0 {
		in.SoakMS = 3000
	}
	if in.HealthWindowMS < 1000 || in.HealthWindowMS > 600000 || in.SoakMS < 500 || in.SoakMS > in.HealthWindowMS {
		fail(w, 400, "health_window_ms must be 1000..600000 and soak_ms 500..health_window_ms")
		return
	}
	id, err := s.Repo.CreateRollout(r.Context(), in.ImageID, in.CanaryGateway, in.HealthWindowMS, in.SoakMS, 3*s.SilentAfter)
	if err != nil {
		fail(w, 409, "%v", err)
		return
	}
	writeJSON(w, 201, map[string]any{"id": id})
}

type stepView struct {
	store.Step
	Transfer *store.Transfer `json:"transfer,omitempty"`
}

func (s *Server) rollout(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.Atoi(r.PathValue("id"))
	if err != nil {
		fail(w, 400, "bad id")
		return
	}
	ro, err := s.Repo.GetRollout(r.Context(), int32(id))
	if err != nil {
		s.dbErr(w, err)
		return
	}
	if ro == nil {
		fail(w, 404, "no such rollout")
		return
	}
	steps := make([]stepView, len(ro.Steps))
	for i, st := range ro.Steps {
		steps[i].Step = st
		if st.State == "transferring" || st.State == "verifying" || st.State == "healthy" || st.State == "failed" || st.State == "rolled_back" {
			steps[i].Transfer, _ = s.Repo.TransferForStep(r.Context(), st.ID)
		}
	}
	ro.Steps = nil
	writeJSON(w, 200, map[string]any{"rollout": ro, "steps": steps})
}

func (s *Server) abortRollout(w http.ResponseWriter, r *http.Request) {
	id, err := strconv.Atoi(r.PathValue("id"))
	if err != nil {
		fail(w, 400, "bad id")
		return
	}
	if err := s.Repo.AbortRollout(r.Context(), int32(id), "aborted by operator"); err != nil {
		fail(w, 409, "%v", err)
		return
	}
	writeJSON(w, 200, map[string]any{"aborted": id})
}

// ---- live updates (SSE) ------------------------------------------------------------------------------------

// Hub fans database NOTIFY events out to SSE clients. Clients receive tiny "something changed" events and refetch.
type Hub struct {
	mu   sync.Mutex
	subs map[chan string]struct{}
}

func NewHub() *Hub { return &Hub{subs: map[chan string]struct{}{}} }

func (h *Hub) Subscribe() (chan string, func()) {
	ch := make(chan string, 64)
	h.mu.Lock()
	h.subs[ch] = struct{}{}
	h.mu.Unlock()
	return ch, func() { h.mu.Lock(); delete(h.subs, ch); h.mu.Unlock() }
}

func (h *Hub) Publish(kind string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	for ch := range h.subs {
		select {
		case ch <- kind:
		default: // slow client: drop; it refetches on the next event anyway
		}
	}
}

func (s *Server) stream(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok || s.Hub == nil {
		fail(w, 500, "streaming unsupported")
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Accel-Buffering", "no")
	ch, unsub := s.Hub.Subscribe()
	defer unsub()
	fmt.Fprint(w, "retry: 1000\n\nevent: hello\ndata: {}\n\n")
	fl.Flush()
	keep := time.NewTicker(15 * time.Second)
	defer keep.Stop()
	for {
		select {
		case k := <-ch:
			fmt.Fprintf(w, "event: change\ndata: %q\n\n", k)
			fl.Flush()
		case <-keep.C:
			fmt.Fprint(w, ": keepalive\n\n")
			fl.Flush()
		case <-r.Context().Done():
			return
		}
	}
}
