// MOCK(M7): UDP listener that decodes FBP and prints. Real version: dedup/reorder, bounded queue, batch COPY into Postgres, Prometheus metrics.
package main

import (
	"fmt"
	"net"

	"github.com/carlosdrojas/fleetbus/backend/internal/fbp"
)

func main() {
	conn, err := net.ListenPacket("udp", ":9000")
	if err != nil {
		panic(err)
	}
	buf := make([]byte, 1500)
	for {
		n, _, err := conn.ReadFrom(buf)
		if err != nil {
			return
		}
		if p, err := fbp.Decode(buf[:n]); err == nil {
			fmt.Printf("MOCK(M7): type=%d src=%d seq=%d\n", p.Type, p.Src, p.Seq)
		}
	}
}
