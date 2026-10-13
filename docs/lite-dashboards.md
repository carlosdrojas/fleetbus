# Dashboards for FleetBus Lite

FleetBus Lite = port milestones P2–P5: two LaunchPads on a CAN bus, no bootloader/OTA/backend (see
`docs/mspm0-port-plan.md`). These are the ways to *see* what the bus is doing. Carlos wants all four (2026-10-12).
Mock-ups of each: the "Lite dashboards" section of the 3D bench page (`docs/mspm0-bench.html`,
https://claude.ai/artifact/PJnjriXdyV3DgxsRAaq5EC). The mock-ups use made-up values, not measurements.

All of them except the LCD read the bus through the optional USB-CAN adapter (CANable): flash it with **slcan**
firmware on macOS (`/dev/tty.usbmodem…`, used through python-can); SocketCAN/`candump` are Linux-only; SavvyCAN is a
free GUI alternative. On a two-node bus the adapter also acknowledges frames, so one board can be tested alone, and
its jumper can be one of the two 120 Ω terminators.

| # | Option | What you see | Status | Effort | When |
|---|---|---|---|---|---|
| 1 | Terminal view | `cantools monitor can/fleetbus.dbc` on the adapter: every frame decoded by name with live values | **works today** (no code) | none | from P3: the debugging tool |
| 2 | Live webpage | `tools/lite_dash.py --can slcan:/dev/tty…`: decodes the bus with the DBC and serves gauges, node states, heartbeat ages and the raw frames over SSE; no database, no internet. `--sim` runs the real firmware logic with a pack-voltage slider and an "unplug battery" switch | **built** (`gateway/fleetbus/lite_dash.py`, 5 tests; not yet run against a real bus) | done | P3–P5, and for a laptop at a career fair without the full stack |
| 3 | Full dashboard | the existing Go backend + Postgres + React dashboard, with `gateway --can slcan:/dev/tty…` pointed at the adapter: history, charts, alerts | **built** (verified in simulation) | setup only | after Lite works on the bench |
| 4 | On-board LCD gauge | an LCD on the body node becomes the car's instrument cluster: voltage needle, temperature, current, state banner, "battery silent" warning; no laptop | **to build** (SPI display driver + low-priority FreeRTOS display task) | a weekend after P5 | Lite's headline feature |

## Option 4 details
- **Screen:** reuse the **ST7735 160×128 SPI color LCD** from the EE319K kit (Space Invaders ran on the same MSPM0
  family). Alternative: SSD1306 128×64 I²C OLED (the body node's I²C bus is free).
- **Firmware:** `bsp_display_*` in `bsp/mspm0g3507/` (SPI + DC/RESET GPIO; pins to choose against the LaunchPad
  schematic), a display task at the lowest priority redrawing ~5×/s from the latest decoded values. The node task must
  keep its 10 ms period while the display draws: measure it (that is the RTOS lesson).
- **Bench check:** the CAN node period and the display frame time with a GPIO toggle on a scope.

## Recommended order
1 during P3 bring-up → 4 after P5 → 2 when you want a laptop view without the backend → 3 when the bootloader/OTA work
(P6–P7) starts and you want history and rollouts.
