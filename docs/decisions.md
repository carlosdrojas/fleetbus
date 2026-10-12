# Stack decisions

Short ADRs for FleetBus's software stack. Each one covers what was chosen, what it was compared against, the tradeoffs, and when to revisit. One-line summaries live in `AGENTS.md` → Decisions.

Decided 2026-10-12 while planning the implementation (plan: milestones M0–M14 in `README.md` → Build order).

---

## D1 — Python gateway, Go backend

**Context.** The gateway talks to the CAN bus through a USB-CAN adapter (CANable) and forwards frames to the backend over FBP, our custom binary UDP protocol. The backend handles UDP ingest, Postgres, the API, alerts, and the OTA rollout orchestrator.

**Decision.** Gateway in **Python** (`python-can`). Backend in **Go**.

| Option | Pros | Cons |
|---|---|---|
| **Python gateway + Go backend (chosen)** | `python-can` is the most mature CAN library and works with the CANable on macOS and Linux and with HIL replay. Go fits long-running network services (goroutines, static binaries, `-race`, built-in fuzzing). Two languages, and Go matches Tesla/SpaceX/Base-style backends. | FBP is implemented **twice**, so the two can drift. Two toolchains and two CI jobs. Slower to the first end-to-end demo. |
| All Python (asyncio + FastAPI) | One FBP implementation; fastest demo. | Weaker "systems backend" signal. asyncio UDP throughput and the GIL become limits in the load test. |
| All Go | One language. | Go CAN libraries are mostly SocketCAN-only, with poor support for macOS and the CANable's slcan mode. HIL tooling is weaker. |

**Mitigation.** `protocol/` holds golden test vectors (bytes ↔ decoded fields) that both the Python and Go test suites must pass, so drift fails CI.

**Revisit if** keeping the two FBP implementations in sync costs more than about a day per milestone.

---

## D2 — Firmware build: CMake + arm-none-eabi-gcc + vendored HAL/FreeRTOS

**Context.** Two (later three) STM32G431RB nodes run FreeRTOS, with a custom bare-metal bootloader and A/B application slots.

**Decision.** Plain **CMake** with an arm-none-eabi toolchain file. STM32CubeG4 HAL/CMSIS, FreeRTOS-Kernel, Monocypher and Unity are vendored as git submodules.

| Option | Pros | Cons |
|---|---|---|
| **CMake + vendored HAL (chosen)** | Builds headless in GitHub Actions. The same tree builds host Unity tests. Clean diffs with no generated-code churn. Per-slot linker scripts for A/B images are easy. Shows build-system skill. | More setup up front: clock config, startup file, linker scripts. Debugging goes through openocd/st-util + VS Code cortex-debug instead of a built-in GUI. |
| STM32CubeIDE project | Fastest bring-up, one-click debugger, pin-config GUI. | Eclipse project files are awkward to build in CI. Generated code mixes with ours. A/B linker variants are clumsy. |
| Zephyr | Built-in CAN stack, MCUboot, devicetree. | Hides exactly the RTOS and bootloader work this project exists to show. |
| PlatformIO | Very easy setup. | Less of an industry standard; thin FreeRTOS + STM32G4 support. |

**Revisit if** bring-up stalls on clock or startup code. In that case generate the init code once with CubeMX and copy it into `bsp/`, still building with CMake.

---

## D3 — Dashboard: single-page web app

**Context.** The dashboard has to show live fleet health and also run OTA: upload a signed image, start or abort a rollout, and watch its steps. It should also work as a phone-sized "car view" over a hotspot.

**Decision.** **Vite + React + TypeScript**, fed by the Go API over REST + Server-Sent Events.

| Option | Pros | Cons |
|---|---|---|
| **SPA (chosen)** | OTA controls and monitoring in one UI. Custom fleet, node and car views. Looks better in the demo video. Shows frontend skill. | More code. Charts have to be built (with a chart library). No ad-hoc queries. |
| Grafana on Postgres | Standard monitoring look, dashboards and alerting in hours, common in industry SRE stacks. | Can't drive OTA workflows, so a second UI is needed anyway. The car view is clunky. |

**Middle path.** The Go services expose Prometheus `/metrics`. A Grafana container can be added to `deploy/docker-compose.yml` later for ops metrics at almost no cost.

**Revisit if** dashboard work starts crowding out phase 4 (OTA). Then cut the SPA to the fleet grid + OTA page and use Grafana for charts.

---

## Smaller defaults

| Choice | Over | Why |
|---|---|---|
| Postgres 16 in Docker | TimescaleDB, SQLite | Plain Postgres with time-partitioned telemetry is enough at this scale; TimescaleDB is a drop-in later. |
| In-process bounded Go channel as the ingest queue | NATS, Redis Streams, Kafka | No extra infrastructure, and backpressure (drops, queue depth) is measurable. NATS is the upgrade path if ingest and the writer split into separate processes. |
| Monocypher for Ed25519 verify | mbedTLS, wolfSSL | Small, single-file, easy to audit. Needed to fit the bootloader flash budget (24 KB TARGET). |
| CRC-32C in FBP | CRC-16 | Stronger error detection for UDP payloads up to MTU size. Cheap, table-driven in C, Python and Go. |
