# FleetBus

A car's network and its cloud, at bench scale.

Two STM32 "ECUs" run FreeRTOS on a real CAN bus. A third node of your own rides in a 2025 Nissan Sentra and reads the car
through the OBD-II port **without ever writing to it**. All of them report to a fleet backend over a custom UDP protocol, and
the backend pushes **signed firmware** back down one car at a time: canary first, health-gated, with the bootloader rolling
back any image that fails its own check.

> **Status (2026-10-12):** the whole software stack is built and verified end to end **in simulation**: the simulator runs the
> real firmware logic. The firmware **cross-compiles and links** for the STM32G431 but has **not run on a board** (parts have not
> arrived). See [`docs/verification.md`](docs/verification.md) for exactly what is measured and what is not.

```
 ┌──────── bench car (desk) ────────┐      ┌──── real vehicle (read-only) ────┐
 │ Battery node      Body node      │      │ Sentra → OBD-II → telematics node│
 │ STM32·FreeRTOS    STM32·FreeRTOS │      │ (allowlisted mode-01 polls only) │
 │      └──── CAN 500 kbit/s ───┘   │      └───────────────┬──── UART ───────┘
 └──────────────┬───────────────────┘                      │ Pi + phone hotspot
          USB-CAN adapter                                   │
                │                                           │
        Gateway (python-can / serial)  ── FBP over UDP: seq #, CRC-32C, ACK/NACK, disk spool, OTA relay
                │                                           │
 ┌──────────────┴──────────── fleet backend (Go + Postgres) ┴───┐
 │ ingest: dedup → reorder → bounded queue → batch COPY → ACK   │  ACK = committed; cursor stored with the data
 │ api: REST + SSE · alerts · rollout orchestrator (fail-closed)│
 │ dashboard (React/TS) · fbctl · /metrics                      │
 └──────────────────────────────────────────────────────────────┘
```

## Try it (no hardware)

```bash
make setup && make db            # python venv, Postgres 16 in Docker (host port 5433)
make test                        # firmware (82 Unity tests), Go (61, -race), Python (68), HIL (5), dashboard build
make e2e                         # 12 scenarios: real ingest + api + Postgres + 3 simulated cars, with processes being killed
make demo                        # dashboard on http://localhost:8080 with 3 simulated cars (make demo-down to stop)
```

In the demo, break things:

```bash
curl -XPOST localhost:7002/sensor -d '{"node":1,"dc":700}'     # overtemp car 2's battery -> SAFE, body -> limp-home, alerts
curl -XPOST localhost:7001/power  -d '{"node":1,"on":false}'   # unplug car 1's battery node -> node_silent alert
scripts/demo.sh images                                           # signed demo firmware: good / hangs at boot / never healthy
build/bin/fbctl rollout start -image 2 -canary 1 && build/bin/fbctl rollout status 1 -watch
```

Bad firmware does not spread: the canary fails its health gate (or the bootloader rolls it back), the rollout halts, the rest of the
fleet is never touched.

## What is in here

| Dir | What |
|---|---|
| `can/` | `fleetbus.dbc`: the single source of truth; C is generated from it (`tools/dbc2c.py`), Python and Go are tested against it |
| `protocol/` | FBP v1 spec and golden vectors that both the Python and Go codecs must pass |
| `firmware/` | `common/` portable C (FSMs, boot A/B logic, Ed25519 image verify, OTA receiver, OBD poller, serial link) + 82 host tests; `bsp/`, `app/`, `bootloader/` for the STM32G431 (FreeRTOS, CMake, vendored HAL as submodules); `host/` simulator |
| `gateway/` | Python: CAN/serial ⇄ FBP/UDP, SQLite spool, retransmit window, OTA relay, sim fleet + fault injection |
| `backend/` | Go: `ingest`, `api` (+ alerts + rollout engine), `fbctl`, `loadgen`; Postgres schema with daily telemetry partitions |
| `dashboard/` | Vite + React + TypeScript: fleet, node charts, updates timeline, phone-sized car view |
| `hil/` | pytest harness: replays recorded traces into a simulated or real board |
| `tools/` | `sign_image.py` (Ed25519), `flash.sh`, `obd_listen.py` (listen-only), generators for the golden vectors |
| `tests/e2e/` | full-stack scenarios |

## Measured so far (this laptop, simulation; details and commands in `docs/verification.md`)

| | |
|---|---|
| Ingest throughput | ~18.5 k CAN frames/s sustained, ~40 k saturated, 0 frames lost (database row count = acknowledged count) |
| Delivery at 50 % packet loss (+ dup + reorder) | 500/500 exactly once, in order |
| Fault → alert visible via API | 1.3 s; node silent → alert 3.0 s (threshold 2 s) |
| Firmware size (arm-gcc 15.3, `-Os`) | bootloader 24.5 KB of 28; apps 37–44 KB of 48.6 KB; RAM 15.3 KB of 22 |

## Rules this repo follows

- **Never write to the car.** The in-car node's only transmit path is `car_tx()`, which allows OBD-II mode 01/09 reads for allowlisted PIDs; CI fails if anything else can transmit. Fault injection and deliberately broken images exist only for the simulator and the bench.
- Every number is measured, or labelled TARGET / ESTIMATED / UNVERIFIED.

## Docs

[`docs/verification.md`](docs/verification.md) (what is proven) · [`docs/bench-guide.md`](docs/bench-guide.md) (what to do when the boards arrive) ·
[`docs/decisions.md`](docs/decisions.md) (ADRs) · [`protocol/spec.md`](protocol/spec.md) · [`docs/proposal.md`](docs/proposal.md) ·
[`docs/career-fit.md`](docs/career-fit.md) · [`docs/toolchain.md`](docs/toolchain.md) · [`AGENTS.md`](AGENTS.md)
