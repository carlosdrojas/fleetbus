package api

import (
	"context"
	"time"

	"github.com/jackc/pgx/v5"

	"github.com/carlosdrojas/fleetbus/backend/internal/store"
)

// ListenPG bridges Postgres LISTEN/NOTIFY to the hub until ctx ends, reconnecting if the connection drops.
func ListenPG(ctx context.Context, dsn string, hub *Hub, logf func(string, ...any)) {
	for ctx.Err() == nil {
		conn, err := pgx.Connect(ctx, dsn)
		if err == nil {
			_, err = conn.Exec(ctx, "LISTEN "+store.NotifyChannel)
		}
		if err != nil {
			logf("api: listen: %v (retrying)", err)
			select {
			case <-time.After(time.Second):
			case <-ctx.Done():
			}
			continue
		}
		for {
			n, err := conn.WaitForNotification(ctx)
			if err != nil {
				conn.Close(context.Background())
				break
			}
			hub.Publish(n.Payload)
		}
	}
}
