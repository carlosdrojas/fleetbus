# FleetBus Lite

Two TI LP-MSPM0G3507 LaunchPads (Arm Cortex-M0+, 80 MHz) act as a car's **battery-monitor ECU** and **body-controller
ECU**. Both run FreeRTOS and talk over a 500 kbit/s CAN bus. The body node goes into **limp-home** when the battery
reports a fault or its heartbeat goes silent, and shows an instrument cluster on a 1.8" ST7735R TFT.

```
   battery node (LaunchPad #1)                         body node (LaunchPad #2)
 ┌──────────────────────────────┐                   ┌──────────────────────────────────┐
 │ pot ─► ADC  (pack voltage)    │                   │ GPIO ─► head / turn / fault lights│
 │ NTC ─► ADC  (temperature)     │   CAN 500 kbit/s  │ PWM  ─► servo                     │
 │ (no current sensor: 0 mA)     │◄═════════════════►│ SPI  ─► ST7735R 160x128 cluster    │
 │ fault FSM: NORMAL→WARNING→    │  0x101 heartbeat  │ limp-home FSM                     │
 │            FAULT→SAFE         │  0x200 BatteryStat│                                   │
 │ FreeRTOS: node/log            │  0x201 BodyStatus │ FreeRTOS: node/disp/log           │
 └──────────────────────────────┘  0x102 heartbeat  └──────────────────────────────────┘
                                    0x300 FaultEvent
```

**Status (2026-11-24): software complete, nothing has run on a board yet.** Hardware numbers go in
[`docs/measured.md`](docs/measured.md) first; anything else here is from the host build and says so.

## What's in the repo

| Path | What | Language |
|---|---|---|
| `firmware/common/` | node logic: CAN messages (`can_msgs.hpp`), battery fault FSM, limp-home FSM, heartbeat monitor, the two node roles, display renderer | C++17 |
| `firmware/app/` | one main per board (`battery_main.cpp`, `body_main.cpp`) with its FreeRTOS tasks, shared pieces (`app_common.*`), bench images (`bringup/`) | C++17, C |
| `firmware/bsp/` | board support on TI DriverLib: clocks, UART, MCAN, ADC, PWM, SPI display | C |
| `firmware/tests/` | Unity tests on the host, including both nodes on a virtual bus | C++17 |
| `firmware/third_party/` | FreeRTOS kernel, Unity (submodules), TI MSPM0 SDK subset (vendored) | C |
| `tools/` | `flash_mspm0.sh` (DSLite), `vendor_mspm0_sdk.sh` | sh |

## Build and test (no hardware)

Needs `brew install cmake ninja` and `brew install --cask gcc-arm-embedded` (the cask, not the formula: the formula has
no newlib-nano and no C++ library).

```bash
make host-test    # node logic on the host + 40 Unity tests
make fw           # cross-build the five MSPM0 images, prints sizes
```

Host build, 2026-11-24: the node images link at 12.1 KB (battery) and 15.6 KB (body) of 128 KB flash, 13.7 KB of 32 KB
SRAM (12 KB of it the FreeRTOS heap). On the virtual bus the body enters limp-home 220 ms after an overtemperature and
420 ms after the battery goes silent. Those are simulated timings, not hardware.

## On the bench

Step by step, with expected values: [`docs/mspm0-bench-steps.md`](docs/mspm0-bench-steps.md).

```bash
tools/flash_mspm0.sh bench bringup        # P2: blink + console
tools/flash_mspm0.sh bench can_selftest   # P3: CAN loopback, then two boards
tools/flash_mspm0.sh node battery         # P5: node firmware (and `node body` on the other board)
screen /dev/cu.usbmodem<XXXX>1 115200     # console
```

No CAN transceivers: the two boards share a diode wired-AND bus (TX through a Schottky, RX on the line, 1 kΩ pull-up),
and an Analog Discovery's CAN decoder watches it.

## Design notes

- **Classic CAN 2.0A at 500 kbit/s**, the rate body buses in cars run. 8-byte frames fit every message. The wire
  format is one hand-written header both boards compile, `firmware/common/can_msgs.hpp`, with a test per byte layout.
- **Heartbeat id = 0x100 + node id.** With one shared id, two nodes that start a heartbeat in the same bit time both win
  arbitration (the ids are identical), then collide on the node-id byte, retry together and collide again.
- **Silence is a fault.** A dead battery ECU can't report its own failure, so the body goes limp-home after 500 ms (5
  missed heartbeats) and needs 1 s of healthy heartbeats before it trusts the battery again. Both are targets until the
  bench measures them.
- **Few FreeRTOS tasks, fast work on top.** Each board has a node task (prio 2, every 10 ms: read inputs, CAN in,
  state machine, outputs, CAN out) and a log task (prio 1, 1 s). The body adds a display task (prio 1, 200 ms). The
  SPI redraw and the ~7 ms console line run below the node task, so they can't delay CAN. The node task prints its
  own worst run time and start lateness, so that's measured, not assumed.
- **Tasks share data through snapshots** (`Latest<T>` in `app_common.hpp`): the writer overwrites, readers copy the
  whole struct with interrupts off for a moment. Readers want the newest value, not a backlog, so no queues and no
  mutexes.
- **The node task feeds the 2 s watchdog** at the end of every cycle. If it hangs, the board resets. A hung display
  task wouldn't, which is acceptable here: the CAN side keeps working.
- **C++17 for the code that's tested, C for the board layer.** Compiled with `-fno-exceptions -fno-rtti
  -fno-threadsafe-statics`; no heap after start-up (`StaticQueue<T, N>`, `std::array`); `constexpr` constructors so
  nothing runs before `main()`; `BatteryNode` and `BodyNode` share `NodeBase` with no virtual functions, and each
  board's main uses only its own; RAII `CriticalSection` so no path leaves interrupts off.
- **TI DriverLib and CMake, not CCS projects.** Builds headless on macOS and in CI; CCS is used only for its DSLite
  flasher.

## Scope

An earlier simulation-only version also had a bootloader, signed OTA, a UDP gateway, a Go/Postgres backend, a web
dashboard and an in-car OBD-II node. None of it ran on hardware; it lives on the `full-platform` branch.
