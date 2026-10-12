#!/usr/bin/env bash
# Start the whole FleetBus stack locally with simulated cars, or stop it.
#   scripts/demo.sh up [CARS]   build everything, start Postgres + ingest + api (+ dashboard) + CARS simulated cars (default 3)
#   scripts/demo.sh down
#   scripts/demo.sh images      build signed demo firmware (good / hangs / never-healthy) into build/demo and print upload commands
# Dashboard: http://localhost:8080   ·   fault injection: curl -XPOST localhost:7001/sensor -d '{"node":1,"dc":700}'
set -euo pipefail
cd "$(dirname "$0")/.."
RUN=build/run; mkdir -p "$RUN" build/bin
DSN="postgres://fleetbus:fleetbus@localhost:5433/fleetbus?sslmode=disable"
PY=.venv/bin/python

case "${1:-up}" in
up)
  CARS="${2:-3}"
  docker compose -f deploy/docker-compose.yml up -d --wait db
  (cd backend && go build -o ../build/bin/ ./cmd/ingest ./cmd/api ./cmd/fbctl ./cmd/loadgen)
  [ -d dashboard/node_modules ] || (cd dashboard && npm install --no-audit --no-fund)
  (cd dashboard && npm run build >/dev/null)
  [ -f build/host/lib/libfbsim.dylib ] || [ -f build/host/lib/libfbsim.so ] || { cmake -S firmware -B build/host -G Ninja >/dev/null && cmake --build build/host >/dev/null; }
  "$0" down >/dev/null 2>&1 || true
  FLEETBUS_DSN="$DSN" build/bin/ingest -listen 127.0.0.1:9000 -metrics 127.0.0.1:9100 >"$RUN/ingest.log" 2>&1 </dev/null & echo $! >"$RUN/ingest.pid"
  FLEETBUS_DSN="$DSN" build/bin/api -listen 127.0.0.1:8080 -static dashboard/dist -pubkey "$($PY -c 'import sys; sys.path.insert(0,"gateway"); from fleetbus.simfleet import sim_key; print(bytes(sim_key().verify_key).hex())')" >"$RUN/api.log" 2>&1 </dev/null & echo $! >"$RUN/api.pid"
  ROOT="$PWD"
  for i in $(seq 1 "$CARS"); do
    ( cd "$ROOT/gateway"
      nohup "$ROOT/$PY" -m fleetbus.gateway --sim --id "$i" --backend 127.0.0.1:9000 --spool "$ROOT/$RUN/spool$i.sqlite" \
        --control "127.0.0.1:$((7000+i))" >"$ROOT/$RUN/gw$i.log" 2>&1 < /dev/null &
      echo $! >"$ROOT/$RUN/gw$i.pid" )
  done
  echo "up: dashboard http://localhost:8080 · $CARS simulated cars · fault injection ports 7001..$((7000+CARS)) · logs in $RUN/"
  ;;
down)
  for f in "$RUN"/*.pid; do [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null || true; rm -f "$f"; done
  echo "stopped"
  ;;
images)
  $PY tools/demo_images.py build/demo >/dev/null
  echo "good:      build/bin/fbctl image upload -a build/demo/body_v2_good_A.signed -b build/demo/body_v2_good_B.signed"
  echo "hangs:     build/bin/fbctl image upload -a build/demo/body_v3_hang_A.signed -b build/demo/body_v3_hang_B.signed"
  echo "unhealthy: build/bin/fbctl image upload -a build/demo/body_v4_unhealthy_A.signed -b build/demo/body_v4_unhealthy_B.signed"
  echo "then:      build/bin/fbctl image list && build/bin/fbctl rollout start -image <ID> -canary 1 && build/bin/fbctl rollout status <N> -watch"
  ;;
*) echo "usage: $0 up [cars] | down | images"; exit 2;;
esac
