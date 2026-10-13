# What is verified, and what is not

Every claim in this repo is **measured** (a command you can run), **TARGET** (a design number not yet measured), or
**UNVERIFIED** (written but needs hardware). Numbers below were measured on the author's MacBook (Apple silicon, macOS,
Postgres 16 in OrbStack) on 2026-10-12 unless noted.

## Verified by running it

| Claim | How it is checked | Command |
|---|---|---|
| CAN contract round-trips in C, Python and Go | DBC → C codegen (`cantools`), Go decoder checked against vectors generated from the DBC | `make host-test`, `go test ./internal/canmsg` |
| Firmware logic: FSMs, boot metadata, A/B select, signature/hash/anti-downgrade verify, OTA receiver, OBD allowlist, serial link | **85 Unity tests** on the host, **run once per board flash map** (STM32G431: 2 KB pages; MSPM0G3507: 1 KB sectors, and the fake flash returns pseudo-random bytes for erased sectors because MSPM0 erased flash is nondeterministic). Includes power-loss injection at every flash operation of a metadata update, an exhaustive 256×256 mode/PID allowlist sweep, one-verify-per-boot, and fresh-board behaviour | `make host-test` |
| The firmware's node logic works as a system: limp-home, OTA, trial boot, rollback | The real C node core + boot logic + fake flash, two nodes on a simulated bus (sim: battery overtemp → body limp-home in 220 ms; battery unplugged → limp-home in 420 ms) | `build/host/tests/test_node_sim` |
| Bad firmware never sticks | Hanging image (watchdog loop) and never-healthy image are rolled back by the boot logic; tampered, unsigned, wrong-key, wrong-node and downgrade images are rejected | `test_node_sim`, `gateway/tests/test_ota_e2e.py` |
| Python signer, C verifier and Go verifier agree on the image format | One golden image signed by Python must verify in C (`fb_verify`) and Go; every tamper case is rejected with the expected code | `gateway/tests/test_sign_image.py`, `go test ./internal/imagefmt` |
| FBP wire format is identical in Python and Go | 9 valid + 8 invalid golden vectors pass in both; CRC-32C check value `0xE3069283`; Go native fuzzing of the decoder (20 s, 2.4 M execs, no crash; decode∘encode is canonical) | `pytest gateway/tests/test_fbp.py`, `go test ./internal/fbp` |
| Reliable delivery over a bad link | 500 packets delivered **exactly once, in order** at 0 / 10 / 30 / 50 % loss (with duplication and reordering) | `gateway/tests/test_reliable.py -s` |
| A gateway crash or ingest outage loses no telemetry | Disk spool + commit-gated ACKs + per-gateway cursor committed in the same transaction as the data (e2e: 6 s ingest outage, every outage second present in Postgres) | `tests/e2e`, `ingest_test.go` |
| Ingest throughput | **~18.5 k CAN frames/s** sustained at 20 k offered, **~40 k frames/s** saturated, loopback, 8 gateways; every acknowledged frame found in Postgres (430,480 / 430,480). Single ingest process. | `make loadgen` (see `backend/cmd/loadgen`) |
| Rollout engine is fail-closed | 10 orchestrator tests (14 cases) with a fake clock (canary never healthy, bootloader rollback, node goes silent, faults after update, flapping health, restart mid-soak, transfer failure/stall, DB error) plus a mutation check: breaking the trial check, the health window or the fault check each makes tests fail | `go test ./internal/rollout` |
| The whole stack works on the MSPM0 flash map too | Same 12 scenarios with `FB_BOARD=mspm0g3507`: simulator on 1 KB sectors with nondeterministic erased flash, MSPM0 hw id in every image: 12/12 in 171.5 s | `FB_BOARD=mspm0g3507 make e2e` |
| The whole stack works with processes dying | 12 end-to-end scenarios with real Postgres, ingest, api, gateways: alerts (fault 1.3 s, node silent 3.0 s), ingest outage replay, good rollout canary→fleet, hanging image rolled back (3 watchdog resets), unhealthy image fails the gate, **api killed mid-rollout** and **ingest killed mid-transfer** both resume, abort, refusal to update an unseen node | `make e2e` |
| Dashboard builds and renders live data | Typecheck + production build; screenshots taken from headless Chrome against the running stack (fleet, alerts, node charts, updates timeline, car view) | `make dashboard`, `scripts/demo.sh up` |
| Firmware cross-compiles and links for MSPM0G3507 (primary) | arm-none-eabi-gcc 15.3, Cortex-M0+, TI MSPM0 SDK 2.11 DriverLib: bootloader 18,080 B of 28 KB; apps 28.7–29.4 KB of 48,640 B; RAM 15.1–15.2 KB of 32 KB; bench images (bring-up, CAN self-test, IO self-test) | `make fw-mspm0` |
| Firmware cross-compiles and links for STM32G431RB (secondary) | Cortex-M4F, STM32 HAL: bootloader 17,552 B of 28 KB; apps 27.3–31.8 KB of 48,640 B; RAM 15.4–15.5 KB of 22 KB. (Before P1 the build silently compiled at `-O3 -fPIC`: 24,500 B and 37–44 KB.) | `make fw` |
| Each board boots only images built for it | Hardware id in the signed header: the MSPM0 verifier accepts MSPM0 images and rejects STM32 ones and vice versa; the API rejects an A/B pair built for different boards | `gateway/tests/test_sign_image.py`, `go test ./internal/api` |
| A freshly flashed board can be provisioned, and only that way | `tools/provision_meta.py` + a signed image → the C bootloader logic (`fb_boot`) boots slot A as TRIAL on both board maps; without the record nothing boots; a wrong-board image is marked BAD even when provisioned | `gateway/tests/test_provisioning.py` |
| Bootloader and OTA verify fit the task stacks | Peak stack of the Ed25519 check measured in an instruction-level emulator: 1,888 B (M0+), 1,664 B (M4). Node/telematics task stacks raised from 1.5 KB to 4 KB after this showed the old size would overflow on the first OTA | `firmware/spike/p0/bench_emu.py` |
| The in-car node cannot transmit anything but allowlisted OBD-II reads | `car_tx()` is the only transmit path; `tools/check_car_tx.sh` fails the build otherwise (proved to fail on a planted raw send); the gateway's serial backend refuses everything but OTA frames for the node itself | `tools/check_car_tx.sh`, `gateway/tests/test_uartlink.py` |

## NOT verified: needs the boards (parts have not arrived)

These are written, compile, and the logic above them is covered by simulation, but **nothing ever ran on silicon**. The bench procedure for each is in `docs/mspm0-bench-steps.md`; results go in `docs/measured.md`.

**MSPM0G3507 LaunchPad (primary):**
- Headless flashing and GDB over the on-board XDS110 (P0 risk R6): OpenOCD with MSPM0 support or TI UniFlash `dslite`, both untried.
- Clock tree: 80 MHz MCLK from SYSPLL with TI's lock-check workaround, 40 MHz HFXT for CAN, UART on a 40 MHz ULPCLK (assumed). MCAN 500 kbit/s timing (computed: prescaler 2, 1+34+5 tq, 87.5 % sample point).
- Pin map (`firmware/bsp/mspm0g3507/bsp_pins.h`), from TI example configs and the datasheet pin table; check against the LaunchPad schematic.
- Flash: sector erase and ECC programming from SRAM, reads through the ECC-uncorrected alias (the fix for nondeterministic erased flash), CPU stall during erase (datasheet: up to 200 ms).
- Watchdog continuity bootloader → app (TI TRM says it cannot be stopped once started, P0 R4), the M0+ jump (VTOR, one NVIC register), boot time (**ESTIMATED** 0.43–0.65 s per image verify at 80 MHz from emulated instruction counts, P0).
- Sensor drivers (ADC0 with hardware averaging, I2C1 INA219, TIMA1 servo PWM polarity) and calibration: NTC curve (**ESTIMATED**), FSM thresholds (**TARGET**).

**STM32G431 Nucleo (secondary, unchanged):** clock tree (170 MHz), FDCAN timing (17 prescaler, 15+4 tq, 80 %), HAL flash and page-erase stall, IWDG, jump, drivers and pins (UM2505).

- CAN bus-off recovery on both boards. Code review in P8 found the STM32 FDCAN driver never left bus-off (the HAL leaves CCCR.INIT set), so one wiring short would have silenced a node until reset; both drivers now leave bus-off on the next poll. Recovery time is a P8 bench measurement.

**Both / car:**
- Whether the Sentra's bus exposes anything to a passive listener, and the engine-off voltage threshold (**TARGET** 13.0 V): day-one measurements, `docs/bench-guide.md`.
- Real low-power sleep for the in-car node. Today it stops polling when the car is off; the OBD-II port stays powered, so **unplug it when parked**.
- CAN bus load and OTA duration over a real 500 kbit/s bus (simulation: a 38 KB image ≈ 6 s; a CANable adds latency).

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
