// Package storetest gives tests a private, migrated Postgres database. Tests skip when Postgres is unreachable
// (set FLEETBUS_TEST_DSN to point somewhere else; default is the docker-compose db on host port 5433).
// Set FLEETBUS_REQUIRE_DB=1 (CI does) to fail instead of skip when the database is missing..
package storetest

import (
	"context"
	"fmt"
	"math/rand"
	"os"
	"testing"
	"time"

	"github.com/jackc/pgx/v5"

	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

func adminDSN() string {
	if d := os.Getenv("FLEETBUS_TEST_DSN"); d != "" {
		return d
	}
	return "postgres://fleetbus:fleetbus@localhost:5433/fleetbus?sslmode=disable"
}

// New returns a store on a fresh database that is dropped when the test ends.
func New(t testing.TB) *store.PG {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	admin, err := pgx.Connect(ctx, adminDSN())
	if err != nil {
		if os.Getenv("FLEETBUS_REQUIRE_DB") != "" {
			t.Fatalf("postgres required but not reachable: %v", err)
		}
		t.Skipf("postgres not reachable (%v); start it with: docker compose -f deploy/docker-compose.yml up -d db", err)
	}
	name := fmt.Sprintf("fbtest_%d_%d", time.Now().UnixNano()%1_000_000_000, rand.Intn(1000))
	if _, err := admin.Exec(ctx, "CREATE DATABASE "+name); err != nil {
		t.Fatal(err)
	}
	cfg := admin.Config().Copy()
	dsn := fmt.Sprintf("postgres://%s:%s@%s:%d/%s?sslmode=disable", cfg.User, cfg.Password, cfg.Host, cfg.Port, name)
	pg, err := store.Open(ctx, dsn)
	if err != nil {
		t.Fatal(err)
	}
	if err := pg.Migrate(ctx); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		pg.Close()
		c2, cancel2 := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel2()
		admin.Exec(c2, "DROP DATABASE IF EXISTS "+name+" WITH (FORCE)")
		admin.Close(c2)
	})
	return pg
}
