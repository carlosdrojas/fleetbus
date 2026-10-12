// ingest: FBP/UDP front door + OTA transfer driver.
package main

import (
	"context"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/carlosdrojas/fleetbus/backend/internal/ingest"
	"github.com/carlosdrojas/fleetbus/backend/internal/metrics"
	"github.com/carlosdrojas/fleetbus/backend/internal/ota"
	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

func main() {
	listen := flag.String("listen", ":9000", "UDP listen address for FBP")
	dsn := flag.String("dsn", env("FLEETBUS_DSN", "postgres://fleetbus:fleetbus@localhost:5433/fleetbus?sslmode=disable"), "Postgres DSN")
	metricsAddr := flag.String("metrics", ":9100", "HTTP address for /metrics (empty to disable)")
	queue := flag.Int("queue", 4096, "bounded ingest queue size")
	batch := flag.Int("batch", 500, "max packets per DB transaction")
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
	reg := metrics.New()
	srv := ingest.New(ingest.Config{Addr: *listen, QueueSize: *queue, BatchMax: *batch}, pg, ingest.NewMetrics(reg))
	if err := srv.Listen(); err != nil {
		log.Fatalf("listen: %v", err)
	}
	log.Printf("ingest listening on %s (udp)", srv.LocalAddr())
	if *metricsAddr != "" {
		mux := http.NewServeMux()
		mux.Handle("/metrics", reg.Handler())
		go func() { log.Println(http.ListenAndServe(*metricsAddr, mux)) }()
	}
	drv := &ota.Driver{Repo: pg, Out: srv, Now: time.Now, Logf: log.Printf}
	go drv.Run(ctx, 100*time.Millisecond)
	if err := srv.Run(ctx); err != nil {
		log.Fatal(err)
	}
}

func env(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}
