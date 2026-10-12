# CAN fleet + OTA project ("FleetBus", working name) — proposal

Visual system map (private artifact): https://claude.ai/artifact/DNPALB9ohPg4tH5UAbDd4j · source: `docs/system-map.html`

Lives in `~/Documents/Dev/fleetbus/` (moved from `Apply/applications/_research/can-fleet-project.md` on 2026-10-04). Rules + decisions: `AGENTS.md`. Career rationale: `docs/career-fit.md`.

Written 2026-10-03. Standalone personal project (445L final project ruled out). Closes the embedded gaps (RTOS, CAN, firmware tests) plus databases/monitoring/networking/security for Tesla SWE, and covers SpaceX SWE via the custom UDP link.

## Pitch
Scale model of a car's electronics + its cloud backend: microcontroller "ECUs" on FreeRTOS talk over CAN; a gateway streams telemetry to a fleet backend; the backend pushes signed firmware that installs over the bus and rolls back automatically if the new version is unhealthy.

## Architecture
```
 ┌──────────────── "the car" (bench) ───────────────┐
 │  Battery Node (STM32)        Body Node (STM32)   │
 │  V/I/temp sensing,           lights + servo,     │
 │  fault states                state machine       │
 │  FreeRTOS + bootloader       FreeRTOS + bootloader│
 │        └──────────── CAN bus ─────────┘          │
 └──────────────────────┬───────────────────────────┘
                        │ USB-CAN adapter
              ┌─────────┴─────────┐
              │ Gateway (laptop,  │  CAN ↔ custom binary protocol
              │ Python)           │  over UDP (seq #, CRC)
              └─────────┬─────────┘
 ┌──────────────── fleet backend ───────────────────┐
 │ UDP ingest → queue → Postgres → API → dashboard  │
 │ health rules + alerts                            │
 │ OTA service: signed images, staged rollout,      │
 │ rollback on failed health check                  │
 └──────────────────────────────────────────────────┘
```

## Components
1. **Nodes (2× STM32 Nucleo-G431RB, FreeRTOS).** Battery node: V/I/temp via ADC + INA219, fault FSM (normal → warning → fault → safe), health broadcasts. Body node: lights + servo FSM, limp-home on battery fault. Tasks: sensing, CAN rx/tx, control, heartbeat; queues; watchdog. Bare-metal bootloader (= Tesla's "bare-metal and RTOS" ask).
2. **CAN protocol.** `.dbc` file (IDs, signals, scaling, version field); heartbeats; fault propagation (behavior when a peer goes silent).
3. **Gateway (Python, `python-can`).** CAN ↔ custom binary protocol over UDP (sequence numbers, CRC, loss/reorder handling) — the SpaceX networking piece.
4. **Fleet backend.** UDP ingest → queue → Postgres (per-node time series) → API → live dashboard (health, firmware version, update history); alert rules (fault spike, node silent).
5. **OTA (centerpiece).** Ed25519-signed images (verify on MCU, e.g. Monocypher); reject unsigned + downgrades (anti-rollback); A/B slots, trial boot, must report healthy within N s or the bootloader rolls back; staged rollout (one node, then the rest), same idea as the Amazon fail-closed gating.
6. **Testing.** Unity unit tests on host in GitHub Actions CI; Python HIL harness replaying recorded CAN traces into the real boards via the USB adapter and asserting state transitions; optional: model the OTA protocol in FleetFlight.
7. **Hardware track (optional; for SpaceX Eng / Tesla EE).** Custom KiCad node board (Nucleo shield): CAN transceiver + termination, sensors (INA219, thermistor, IMU), regulator + protection, connectors, test points. Bring-up log with scope captures (CAN signal integrity, rail ripple, ADC noise) compared against expectations.

## Phases (each is a valid stopping point)
| Phase | Weeks | Deliverable | Unlocks |
|---|---|---|---|
| 1. Bus up | 1–2 | FreeRTOS on both nodes, versioned CAN messages, .dbc | RTOS + CAN |
| 2. Tested | 2–3 | Unity tests in CI, HIL replay harness | Firmware tests + HIL → embedded resume fixed |
| 3. Fleet | 3–5 | UDP gateway, Postgres, dashboard, alerts | Databases, networking, monitoring |
| 4. OTA | 5–8 | Bootloader, signed images, A/B, rollback | Updates, security, bare-metal + RTOS |
| 4b. In-car node (optional) | +2 | Your STM32 telematics node lives in the Sentra (12 V, OBD-II read-only), reports over a phone hotspot, and takes the same signed, rollback-safe OTA | OTA on a real vehicle-mounted device; the strongest demo |
| 5. Hardware | +3–4 | Custom PCB, bring-up, measurements | PCB/circuit design, bench validation |

## Parts (~$60–80; +$30–50 for custom PCB; verify prices)
2× STM32 Nucleo-G431RB · 2× CAN transceiver (SN65HVD230 / TCAN1042) · 1× USB-CAN adapter (CANable) · INA219, thermistor, small servo, LEDs, jumpers.

## Coverage
Tesla Embedded (Optimus, Firmware Platforms, AI Platforms, New Programs) · Tesla Update Systems / Validation · Tesla AI Data Infra / Diagnostics · Tesla Red Team / Product Security · **SpaceX SWE** · SpaceX Eng / Tesla EE only with phase 5. Not covered: controls theory (HV, Steering), camera/ML (Camera & Robotics).

## Risks
- Bootloader + A/B flash layout is the hardest part → it's phase 4, after a resume-worthy stop point.
- CAN bit timing / termination → have a scope or logic analyzer on day one.
- Bench only. Never connect to a real vehicle's CAN bus.

## Real vehicle: 2025 Nissan Sentra SR (added 2026-10-03)
- **Role:** third, real data source. The **car is read-only**: Car → OBD-II → your telematics node → in-car link (Raspberry Pi or WiFi board + phone hotspot) → fleet backend. Early on, plugging the USB-CAN adapter straight into the OBD-II port is fine.
- **Your node in the car can be updated (phase 4b).** It's your device and only ever reads from the car, so it takes the same signed, A/B, rollback-safe OTA as the bench nodes. Demo: push an update to a device in a real car and watch it roll back when the new version fails.
- **Bench vs car:** the bench is where you break things on purpose (fault injection, dead heartbeats, deliberately broken images). The car is where you prove it works in the real world.
- **Expect:** standard OBD-II PIDs over CAN (500 kbps, emissions-mandated): speed, RPM, throttle, coolant/intake temp, engine load, fuel level, battery voltage.
- **Unknown:** whether passive sniffing works or the OBD port is gateway-filtered. Day-one test: plug in listen-only and see if frames arrive without requests. Either outcome works.
- **Cable:** standard OBD-II → DB9 / bare-wire for the USB-CAN adapter (~$10–15). No special harness (not a Tesla).
- **Adds:** drive-state FSM (parked → moving → hard brake → cornering), optional IMU fusion.
- **Hardware track upgrade:** telematics node powered from car 12 V. Reverse-polarity + transient protection, regulator to 3.3 V; ties to the ECE 302 buck converter. Sleep or unplug when parked (OBD is always-on → battery drain).
- **In-car dashboard (optics):** (1) phone on a dash mount showing a "car view" of the fleet dashboard over WiFi (no new hardware); (2) small SPI TFT gauge on the telematics node (speed/RPM/drive state); (3) RPi + 7" touchscreen head unit (optional, separate build). Demo video: driving with the dash gauge live while the cloud dashboard updates on a laptop.
- **Rules:** standard diagnostic requests only; never write/flash/fuzz the **car's** bus or modules (Nissan's ECUs run locked firmware anyway). Updating **your own** node inside the car is fine. Fault injection stays on the bench. Screens glanceable only; a passenger operates/films; never mount on the windshield.

## Flight computer
Not required by SpaceX. Optional later: reuse this firmware + flight-phase FSM in a rocket payload only if SpaceX Engineering becomes the top target (confirm ITAR eligibility first).
