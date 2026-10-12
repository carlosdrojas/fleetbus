package api_test

import (
	"bytes"
	"context"
	"crypto/ed25519"
	"encoding/json"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/api"
	"github.com/carlosdrojas/fleetbus/backend/internal/imagefmt"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
	"github.com/carlosdrojas/fleetbus/backend/internal/store/storetest"
)

type env struct {
	srv *httptest.Server
	pg  *store.PG
	hub *api.Hub
	pub ed25519.PublicKey
	prv ed25519.PrivateKey
}

func setup(t *testing.T) *env {
	pg := storetest.New(t)
	pub, prv, _ := ed25519.GenerateKey(nil)
	hub := api.NewHub()
	s := &api.Server{Repo: pg, Hub: hub, PublicKey: pub}
	srv := httptest.NewServer(s.Handler())
	t.Cleanup(srv.Close)
	return &env{srv, pg, hub, pub, prv}
}

func (e *env) get(t *testing.T, path string, v any) int {
	r, err := http.Get(e.srv.URL + path)
	if err != nil {
		t.Fatal(err)
	}
	defer r.Body.Close()
	if v != nil {
		json.NewDecoder(r.Body).Decode(v)
	}
	return r.StatusCode
}

func (e *env) upload(t *testing.T, a, b []byte) (int, string) {
	var buf bytes.Buffer
	mw := multipart.NewWriter(&buf)
	if a != nil {
		fw, _ := mw.CreateFormFile("slot_a", "a")
		fw.Write(a)
	}
	if b != nil {
		fw, _ := mw.CreateFormFile("slot_b", "b")
		fw.Write(b)
	}
	mw.Close()
	r, err := http.Post(e.srv.URL+"/api/images", mw.FormDataContentType(), &buf)
	if err != nil {
		t.Fatal(err)
	}
	defer r.Body.Close()
	body, _ := io.ReadAll(r.Body)
	return r.StatusCode, string(body)
}

func (e *env) seedFleet(t *testing.T, age time.Duration, state int16, trial bool) {
	now := time.Now().Add(-age)
	err := e.pg.WriteBatch(context.Background(), store.Batch{
		Gateways:   []store.GatewayUpdate{{ID: 1, TS: now, Simulated: true}},
		Heartbeats: []store.NodeHeartbeat{{TS: now, GatewayID: 1, NodeID: 2, FwVersion: 1, Slot: 0, Trial: trial, State: state}},
	})
	if err != nil {
		t.Fatal(err)
	}
}

func TestFleetHealthClassification(t *testing.T) {
	cases := []struct {
		name  string
		age   time.Duration
		state int16
		trial bool
		want  string
	}{
		{"normal", 0, 1, false, "ok"}, {"warning", 0, 2, false, "warning"}, {"limp", 0, 5, false, "warning"}, {"trial", 0, 1, true, "warning"},
		{"fault", 0, 3, false, "critical"}, {"safe", 0, 4, false, "critical"}, {"silent", 10 * time.Second, 1, false, "silent"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			e := setup(t)
			e.seedFleet(t, c.age, c.state, c.trial)
			var out struct {
				Gateways []struct {
					Simulated bool
					Nodes     []struct {
						Health    string
						StateName string `json:"state_name"`
						SlotName  string `json:"slot_name"`
					}
				}
			}
			if code := e.get(t, "/api/fleet", &out); code != 200 {
				t.Fatal(code)
			}
			n := out.Gateways[0].Nodes[0]
			if n.Health != c.want || !out.Gateways[0].Simulated || n.SlotName != "A" {
				t.Fatalf("%+v", n)
			}
		})
	}
}

func TestUploadValidation(t *testing.T) {
	e := setup(t)
	body := bytes.Repeat([]byte{7}, 500)
	a, b := imagefmt.Build(body, 2, 0, 3, e.prv), imagefmt.Build(body, 2, 1, 3, e.prv)
	if code, msg := e.upload(t, a, b); code != 201 || !strings.Contains(msg, `"verified":true`) {
		t.Fatalf("good upload: %d %s", code, msg)
	}
	if code, _ := e.upload(t, a, b); code != 409 {
		t.Fatalf("duplicate version must be 409, got %d", code)
	}
	_, other, _ := ed25519.GenerateKey(nil)
	bad := map[string][2][]byte{
		"missing slot b":   {a, nil},
		"swapped slots":    {b, a},
		"wrong key":        {imagefmt.Build(body, 2, 0, 4, other), imagefmt.Build(body, 2, 1, 4, other)},
		"version mismatch": {imagefmt.Build(body, 2, 0, 5, e.prv), imagefmt.Build(body, 2, 1, 6, e.prv)},
		"node mismatch":    {imagefmt.Build(body, 1, 0, 7, e.prv), imagefmt.Build(body, 2, 1, 7, e.prv)},
		"garbage":          {[]byte("not an image"), []byte("nope")},
	}
	for name, p := range bad {
		if code, msg := e.upload(t, p[0], p[1]); code < 400 || code >= 500 {
			t.Errorf("%s accepted or server error: %d %s", name, code, msg)
		}
	}
	var imgs []store.Image
	e.get(t, "/api/images", &imgs)
	if len(imgs) != 1 {
		t.Fatalf("only the good image may be stored, have %d", len(imgs))
	}
}

func TestRolloutValidationAndLifecycle(t *testing.T) {
	e := setup(t)
	e.seedFleet(t, 0, 1, false)
	body := bytes.Repeat([]byte{7}, 500)
	e.upload(t, imagefmt.Build(body, 2, 0, 2, e.prv), imagefmt.Build(body, 2, 1, 2, e.prv))
	post := func(path, js string) (int, string) {
		r, err := http.Post(e.srv.URL+path, "application/json", strings.NewReader(js))
		if err != nil {
			t.Fatal(err)
		}
		defer r.Body.Close()
		b, _ := io.ReadAll(r.Body)
		return r.StatusCode, string(b)
	}
	for name, js := range map[string]string{"no image": `{}`, "bad window": `{"image_id":1,"health_window_ms":10}`, "soak > window": `{"image_id":1,"health_window_ms":2000,"soak_ms":5000}`, "not json": `x`} {
		if code, _ := post("/api/rollouts", js); code != 400 {
			t.Errorf("%s: %d", name, code)
		}
	}
	if code, _ := post("/api/rollouts", `{"image_id":99}`); code != 409 {
		t.Errorf("unknown image: %d", code)
	}
	if code, _ := post("/api/rollouts", `{"image_id":1,"canary_gateway":5}`); code != 409 {
		t.Errorf("offline canary must be refused: %d", code)
	}
	code, msg := post("/api/rollouts", `{"image_id":1}`)
	if code != 201 {
		t.Fatalf("%d %s", code, msg)
	}
	if code, _ := post("/api/rollouts", `{"image_id":1}`); code != 409 {
		t.Fatal("second rollout for the same node type must be refused")
	}
	var ro struct {
		Rollout struct{ State string }
		Steps   []struct{ State, Phase string }
	}
	e.get(t, "/api/rollouts/1", &ro)
	if ro.Rollout.State != "running" || len(ro.Steps) != 1 || ro.Steps[0].Phase != "canary" {
		t.Fatalf("%+v", ro)
	}
	if code, _ := post("/api/rollouts/1/abort", ``); code != 200 {
		t.Fatalf("abort: %d", code)
	}
	if code, _ := post("/api/rollouts/1/abort", ``); code != 409 {
		t.Fatalf("aborting twice: %d", code)
	}
	if e.get(t, "/api/rollouts/42", nil) != 404 {
		t.Fatal("unknown rollout must be 404")
	}
}

func TestMetricsFaultsAlertsEndpointsReturnArraysNotNull(t *testing.T) {
	e := setup(t)
	for _, p := range []string{"/api/faults", "/api/events", "/api/alerts", "/api/images", "/api/rollouts", "/api/nodes/1/1/metrics?name=x"} {
		r, _ := http.Get(e.srv.URL + p)
		b, _ := io.ReadAll(r.Body)
		r.Body.Close()
		if strings.TrimSpace(string(b)) != "[]" {
			t.Errorf("%s: %q (the dashboard maps over these; null would crash it)", p, b)
		}
	}
	if e.get(t, "/api/nodes/x/1/metrics?name=a", nil) != 400 {
		t.Error("bad gateway id must be 400")
	}
}

func TestSSEDeliversChangesAndHealthz(t *testing.T) {
	e := setup(t)
	if e.get(t, "/healthz", nil) != 200 {
		t.Fatal("healthz")
	}
	r, err := http.Get(e.srv.URL + "/api/stream")
	if err != nil {
		t.Fatal(err)
	}
	defer r.Body.Close()
	if ct := r.Header.Get("Content-Type"); ct != "text/event-stream" {
		t.Fatal(ct)
	}
	buf := make([]byte, 512)
	go func() { time.Sleep(100 * time.Millisecond); e.hub.Publish("nodes") }()
	var got string
	for !strings.Contains(got, "change") {
		r.Body.(io.Reader).Read(buf)
		got += string(buf[:bytes.IndexByte(append(buf, 0), 0)])
		if len(got) > 2000 {
			break
		}
	}
	if !strings.Contains(got, "event: hello") || !strings.Contains(got, `data: "nodes"`) {
		t.Fatalf("stream: %q", got)
	}
}
