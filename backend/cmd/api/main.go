// MOCK(M8): returns canned fleet JSON. Real version: REST + SSE from Postgres LISTEN/NOTIFY, alert rules, /metrics.
package main

import (
	"fmt"
	"net/http"
)

func main() {
	http.HandleFunc("/api/fleet", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprint(w, `{"mock":true,"nodes":[{"id":1,"name":"battery","fw":"0.0.0","health":"ok","simulated":true},{"id":2,"name":"body","fw":"0.0.0","health":"ok","simulated":true}]}`)
	})
	http.ListenAndServe(":8080", nil)
}
