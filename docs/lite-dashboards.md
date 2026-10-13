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
| 4 | On-board LCD gauge | an ST7735 on the body node becomes the car's instrument cluster: voltage needle over red/amber/green zones at the firmware thresholds, temperature, current, SoC, state banner (NORMAL / WARNING / FAULT / SAFE / BATTERY SILENT / NO DATA / LIMP HOME); no laptop | **built** (renderer `common/gauge_view.c` host-tested, 6 tests; ST7735 driver + display task on MSPM0; **not run on a board**) | bench bring-up only | Lite's headline feature |

## Option 4 details
- **Screen:** Carlos's **Adafruit 1.8" TFT breakout** (product 358: ST7735R, 160×128, RGB565). Wiring in
  `firmware/bsp/mspm0g3507/bsp_pins.h` and `docs/lite.md` L3: SCK PB9, MOSI PB8 (SPI1), TFT_CS PB6, D/C PB16, RESET PB15,
  LITE to 3.3 V. Default black-tab panel settings; `-DFB_LCD_GREENTAB=ON` for green-tab panels.
- **Renderer:** `firmware/common/gauge_view.c` draws only through a fill-rectangle callback and repaints only what
  changed. Host tests check every banner, clamping, that 300 random incremental updates always equal a fresh full
  draw, and that an unchanged frame costs one call. `tools/gauge_shots.py` renders the real output to `docs/img/`.
- **Firmware:** `bsp/mspm0g3507/bsp_display.c` (ST7735R init, RGB565, landscape; `LCD_MADCTL`/offsets to adjust per
  panel) and a display task in the body app (`FB_HAS_DISPLAY`, lowest priority, 5×/s; the watchdog also requires it
  to make progress). The node task must keep its 10 ms period while the display draws: the status line prints
  `disp_ms` (that is the RTOS lesson). Body image with the display: 32.7 KB.
- **Bench check:** the CAN node period and the display frame time with a GPIO toggle on a scope.

## Recommended order
1 during P3 bring-up → 4 after P5 → 2 when you want a laptop view without the backend → 3 when the bootloader/OTA work
(P6–P7) starts and you want history and rollouts.
