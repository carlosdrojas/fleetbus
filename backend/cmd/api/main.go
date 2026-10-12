// api: REST + SSE for the dashboard and fbctl, plus the alert engine and the staged-rollout orchestrator.
package main

import (
	"context"
	"crypto/ed25519"
	"encoding/hex"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"regexp"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/alerts"
	"github.com/carlosdrojas/fleetbus/backend/internal/api"
	"github.com/carlosdrojas/fleetbus/backend/internal/metrics"
	"github.com/carlosdrojas/fleetbus/backend/internal/rollout"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

func main() {
	listen := flag.String("listen", ":8080", "HTTP listen address")
	dsn := flag.String("dsn", env("FLEETBUS_DSN", "postgres://fleetbus:fleetbus@localhost:5433/fleetbus?sslmode=disable"), "Postgres DSN")
	static := flag.String("static", "", "directory with the built dashboard (dashboard/dist)")
	pubHex := flag.String("pubkey", "", "Ed25519 public key (hex) used to verify uploaded images")
	pubHdr := flag.String("pubkey-header", "", "path to firmware/keys/fleetbus_pub.h (alternative to -pubkey)")
	silent := flag.Duration("node-silent-after", 2*time.Second, "node considered silent after this long")
	flag.Parse()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	pg, err := store.Open(ctx, *dsn)
	if err != nil {
		log.Fatalf("database: %v", err)
	}
	defer pg.Close()
	if err := pg.Migrate(ctx); err != nil {
		log.Fatalf("migrate: %v", err)
	}
	var pub ed25519.PublicKey
	switch {
	case *pubHex != "":
		b, err := hex.DecodeString(strings.TrimSpace(*pubHex))
		if err != nil || len(b) != 32 {
			log.Fatal("-pubkey must be 64 hex chars")
		}
		pub = b
	case *pubHdr != "":
		pub = readHeaderKey(*pubHdr)
	default:
		log.Println("WARNING: no -pubkey: uploaded images are structurally checked but their signatures are not verified here (devices still verify)")
	}

	reg := metrics.New()
	hub := api.NewHub()
	go api.ListenPG(ctx, *dsn, hub, log.Printf)
	go (&alerts.Engine{Repo: pg, Cfg: alerts.Config{NodeSilentAfter: *silent}, Now: time.Now, Logf: log.Printf}).Run(ctx, time.Second)
	go (&rollout.Engine{Repo: pg, Now: time.Now, Logf: log.Printf}).Run(ctx, 250*time.Millisecond)

	s := &api.Server{Repo: pg, Hub: hub, Reg: reg, PublicKey: pub, Static: *static, SilentAfter: *silent}
	h := &http.Server{Addr: *listen, Handler: s.Handler()}
	go func() {
		<-ctx.Done()
		sc, c := context.WithTimeout(context.Background(), 3*time.Second)
		defer c()
		h.Shutdown(sc)
	}()
	log.Printf("api listening on %s", *listen)
	if err := h.ListenAndServe(); err != http.ErrServerClosed {
		log.Fatal(err)
	}
}

func readHeaderKey(path string) ed25519.PublicKey {
	b, err := os.ReadFile(path)
	if err != nil {
		log.Fatal(err)
	}
	var key []byte
	for _, m := range regexp.MustCompile(`0x([0-9a-fA-F]{2})`).FindAllStringSubmatch(string(b), -1) {
		v, _ := strconv.ParseUint(m[1], 16, 8)
		key = append(key, byte(v))
	}
	if len(key) != 32 {
		log.Fatalf("%s: found %d key bytes, want 32", path, len(key))
	}
	return key
}

func env(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}
