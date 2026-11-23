# FleetBus Lite

Two TI LP-MSPM0G3507 LaunchPads (Arm Cortex-M0+, 80 MHz) act as a car's **battery-monitor ECU** and **body-controller
ECU**. Both run FreeRTOS and talk over a 500 kbit/s CAN bus. The body node goes into **limp-home** when the battery
reports a fault or its heartbeat goes silent, and shows an instrument cluster on a 1.8" TFT. A laptop decodes the bus
live.

```
   battery node (LaunchPad #1)                         body node (LaunchPad #2)
 ┌──────────────────────────────┐                   ┌──────────────────────────────────┐
 │ pot ─► ADC  (pack voltage)    │                   │ GPIO ─► head / turn / fault lights│
 │ NTC ─► ADC  (temperature)     │   CAN 500 kbit/s  │ PWM  ─► servo                     │
 │ INA219 ─► I2C (current, opt.) │◄═════════════════►│ SPI  ─► ST7735R 160x128 cluster    │
 │ fault FSM: NORMAL→WARNING→    │  0x101 heartbeat  │ limp-home FSM                     │
 │            FAULT→SAFE         │  0x200 BatteryStat│                                   │
 │ FreeRTOS: wdg/node/sense/log  │  0x201 BodyStatus │ FreeRTOS: wdg/node/disp/log       │
 └──────────────────────────────┘  0x102 heartbeat  └──────────────────────────────────┘
                                    0x300 FaultEvent
                       │
                       └── laptop: CANable or logic analyser ─► tools/lite_dash.py (decodes with the same DBC)
```

**Status (2026-11-23): software complete, nothing has run on a board yet.** Every number below is from the host build
or the simulator, and says so. Hardware numbers go in [`docs/measured.md`](docs/measured.md) first.

## What's in the repo

| Path | What | Language |
|---|---|---|
| `can/fleetbus.dbc` | the CAN contract: 4 messages, single source of truth | DBC |
| `firmware/common/` | portable node logic: fault FSM, limp-home FSM, heartbeat monitor, nodes, display renderer | C++17 |
| `firmware/common/can_msgs.{h,c}` | pack/unpack generated from the DBC by `tools/dbc2c.py` (cantools) | C (generated) |
| `firmware/app/` | FreeRTOS tasks (`app_main.cpp`), role glue (`battery_hw.cpp`, `body_hw.cpp`), bench images (`bringup/`) | C++17, C |
| `firmware/bsp/` | board support on TI DriverLib: clocks, UART, MCAN, ADC, I2C, PWM, SPI display | C |
| `firmware/host/` | host simulator (C ABI for Python) and the display renderer CLI | C++17 |
| `firmware/tests/` | Unity tests on the host | C++17, C |
| `dash/` | live web dashboard: decodes the bus (CANable) or runs the simulator | Python |
| `firmware/third_party/` | FreeRTOS kernel, Unity (submodules), TI MSPM0 SDK subset (vendored) | C |

Why C++ for the logic and C for the board layer: [`docs/cpp-firmware.md`](docs/cpp-firmware.md). Design decisions and
rejected alternatives: [`docs/decisions.md`](docs/decisions.md).

## Build and test (no hardware)

```bash
make setup        # .venv with cantools / python-can
make host-test    # node logic on the host + 39 Unity tests
make py-test      # dashboard tests against the simulator
make fw           # cross-build the five MSPM0 images (arm-none-eabi-gcc/g++), prints sizes
make dash         # dashboard on http://127.0.0.1:8000 against the simulator: drag the pack-voltage slider, "unplug" the battery
```

Measured on the host build (2026-11-23): node images link at 12.0 KB (battery) and 15.5 KB (body) of 128 KB flash;
13.7 KB of 32 KB SRAM, 12 KB of it the FreeRTOS heap. In the simulator the body enters limp-home 220 ms after an
overtemperature and 420 ms after the battery is unplugged (`build/host/tests/test_node`). Simulator numbers, not hardware.

## On the bench

Step by step, with expected values: [`docs/mspm0-bench-steps.md`](docs/mspm0-bench-steps.md).

```bash
make fw
tools/flash_mspm0.sh bench bringup        # P2: blink + console
tools/flash_mspm0.sh bench can_selftest   # P3: CAN loopback, then two boards
tools/flash_mspm0.sh node battery         # P5: node firmware (and `node body` on the other board)
screen /dev/cu.usbmodem<XXXX>1 115200     # console
tools/lite_dash.py --can slcan:/dev/cu.usbmodem<YYYY>   # dashboard on the real bus (CANable, receive only)
```

## Scope

FleetBus Lite is the whole project. An earlier simulation-only version also had a bootloader, signed OTA, a UDP
gateway, a Go/Postgres backend, a React dashboard and an in-car OBD-II node; none of it ran on hardware, and it lives
on the `full-platform` branch, not here.
