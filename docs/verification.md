# What is verified, and what is not

Every claim in this repo is **measured** (a command you can run), **TARGET** (a design number not yet measured), or
**UNVERIFIED** (written but needs hardware). Numbers below were measured on the author's MacBook (Apple silicon, macOS,
Postgres 16 in OrbStack) on 2026-10-12 unless noted.

## Verified by running it

| Claim | How it is checked | Command |
|---|---|---|
| CAN contract round-trips in C, Python and Go | DBC → C codegen (`cantools`), Go decoder checked against vectors generated from the DBC | `make host-test`, `go test ./internal/canmsg` |
| Firmware logic: FSMs, boot metadata, A/B select, signature/hash/anti-downgrade verify, OTA receiver, OBD allowlist, serial link | **82 Unity tests** on the host, incl. power-loss injection at every flash operation of a metadata update, and an exhaustive 256×256 mode/PID allowlist sweep | `make host-test` |
| The firmware's node logic works as a system: limp-home, OTA, trial boot, rollback | The real C node core + boot logic + fake flash, two nodes on a simulated bus (sim: battery overtemp → body limp-home in 220 ms; battery unplugged → limp-home in 420 ms) | `build/host/tests/test_node_sim` |
| Bad firmware never sticks | Hanging image (watchdog loop) and never-healthy image are rolled back by the boot logic; tampered, unsigned, wrong-key, wrong-node and downgrade images are rejected | `test_node_sim`, `gateway/tests/test_ota_e2e.py` |
| Python signer, C verifier and Go verifier agree on the image format | One golden image signed by Python must verify in C (`fb_verify`) and Go; every tamper case is rejected with the expected code | `gateway/tests/test_sign_image.py`, `go test ./internal/imagefmt` |
| FBP wire format is identical in Python and Go | 9 valid + 8 invalid golden vectors pass in both; CRC-32C check value `0xE3069283`; Go native fuzzing of the decoder (20 s, 2.4 M execs, no crash; decode∘encode is canonical) | `pytest gateway/tests/test_fbp.py`, `go test ./internal/fbp` |
| Reliable delivery over a bad link | 500 packets delivered **exactly once, in order** at 0 / 10 / 30 / 50 % loss (with duplication and reordering) | `gateway/tests/test_reliable.py -s` |
| A gateway crash or ingest outage loses no telemetry | Disk spool + commit-gated ACKs + per-gateway cursor committed in the same transaction as the data (e2e: 6 s ingest outage, every outage second present in Postgres) | `tests/e2e`, `ingest_test.go` |
| Ingest throughput | **~18.5 k CAN frames/s** sustained at 20 k offered, **~40 k frames/s** saturated, loopback, 8 gateways; every acknowledged frame found in Postgres (430,480 / 430,480). Single ingest process. | `make loadgen` (see `backend/cmd/loadgen`) |
| Rollout engine is fail-closed | 10 orchestrator tests (14 cases) with a fake clock (canary never healthy, bootloader rollback, node goes silent, faults after update, flapping health, restart mid-soak, transfer failure/stall, DB error) plus a mutation check: breaking the trial check, the health window or the fault check each makes tests fail | `go test ./internal/rollout` |
| The whole stack works with processes dying | 12 end-to-end scenarios with real Postgres, ingest, api, gateways: alerts (fault 1.3 s, node silent 3.0 s), ingest outage replay, good rollout canary→fleet, hanging image rolled back (3 watchdog resets), unhealthy image fails the gate, **api killed mid-rollout** and **ingest killed mid-transfer** both resume, abort, refusal to update an unseen node | `make e2e` |
| Dashboard builds and renders live data | Typecheck + production build; screenshots taken from headless Chrome against the running stack (fleet, alerts, node charts, updates timeline, car view) | `make dashboard`, `scripts/demo.sh up` |
| Firmware cross-compiles and links for STM32G431RB | arm-none-eabi-gcc 15.3: bootloader 24,500 B of 28 KB (85 %); apps 37–44 KB of 48,640 B (76–90 %); RAM 15.3 KB of 22 KB | `make fw` |
| The in-car node cannot transmit anything but allowlisted OBD-II reads | `car_tx()` is the only transmit path; `tools/check_car_tx.sh` fails the build otherwise (proved to fail on a planted raw send); the gateway's serial backend refuses everything but OTA frames for the node itself | `tools/check_car_tx.sh`, `gateway/tests/test_uartlink.py` |

## NOT verified: needs the boards (parts have not arrived)

These are written, compile, and are covered by simulation, but **never ran on silicon**:

- Clock tree (170 MHz), FDCAN 500 kbit/s bit timing (computed: 17 prescaler, 15+4 tq, 80 % sample point), UART, ADC/INA219/servo drivers, pin assignments (taken from the datasheet; check against UM2505).
- Flash erase/program through the HAL, and the CPU stall during a page erase on the single-bank G431 (design assumption ≈ 22 ms/page, **ESTIMATED**; the OTA receiver erases one page at a time so a stall stays below the 500 ms peer timeout).
- Bootloader jump, IWDG behaviour, trial-boot rollback timing on real hardware, Ed25519 verify time on the MCU (**TARGET**: < 100 ms at 170 MHz; unmeasured).
- Sensor calibration: NTC curve (**ESTIMATED**, beta equation), pack-voltage pot mapping, FSM thresholds (**TARGET**, placeholders).
- Whether the Sentra's bus exposes anything to a passive listener, and the engine-off voltage threshold (**TARGET** 13.0 V): both are day-one measurements. See `docs/bench-guide.md`.
- Real low-power sleep for the in-car node. Today it stops polling when the car is off; the OBD-II port stays powered, so **unplug it when parked**.
- CAN bus load and OTA duration over a real 500 kbit/s bus (simulation says a 38 KB image is ~6 s; a CANable adds latency).

## Not built

- Phase 5, custom KiCad PCB (`hardware/`): optional, no design exists.
- GitHub Actions workflow (`.github/workflows/ci.yml`) is written but has not run on GitHub.
- Grafana, TimescaleDB, multi-region anything. Single ingest process; `fleetbus_ingest_queue_full_waits_total` tells you when to split it.

## Known limitations / design notes

- A gateway whose spool was lost restarts numbering at 1; ingest treats a *first-send* seq 1 after > 64 acked packets as a new session. A gateway far ahead of a database that was wiped is adopted at its current seq (anything lower that straggles in is dropped).
- ACK = "committed to Postgres", not "received". During a DB outage ACKs stop and gateways spool (bounded by 64 MB, newest dropped and counted).
- The battery FSM latches FAULT/SAFE until power-cycled by design; alerts clear after the node reboots.
- The image key shipped in `firmware/keys/fleetbus_pub.h` is a **dev key** generated on the author's machine; the private half is in `~/.fleetbus/keys/dev.key` and is not in the repo. Replace it (`tools/sign_image.py keygen --force`) before anything leaves the bench.
- The simulator trusts the *demo* key (`FLEETBUS_SIM_KEY` overrides). Demo images only run in the simulator.
