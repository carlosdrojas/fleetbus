# FleetBus Lite

**This is the version being built on hardware** (decision 2026-10-13). Its software is complete on `main`; nothing has run
on a board yet.

The smallest version of FleetBus that still covers an RTOS and CAN: two TI LP-MSPM0G3507 LaunchPads run FreeRTOS and
talk over a 500 kbit/s CAN bus. The battery node measures and broadcasts; the body node drives lights, a servo and an
LCD instrument cluster, and goes into limp-home when the battery faults or goes silent. No bootloader, no OTA, no
backend. Everything beyond Lite (P6–P8 bootloader/OTA/fault injection, the gateway and fleet backend) builds on it
without changes.

Nothing here has run on a board yet. Every expected number is from TI documents, arithmetic, or simulation, and says so.
Record what you measure in `docs/measured.md`.

## What runs where

| Node | FreeRTOS tasks | CAN out | Hardware |
|---|---|---|---|
| Battery (LaunchPad 1) | node 10 ms (prio 3), sense 20 ms (prio 2), watchdog 100 ms (prio 4) | Heartbeat 0x100 (100 ms), BatteryStatus 0x200 (100 ms), FaultEvent 0x300 | pot (pack voltage), NTC (temperature), INA219 (current) |
| Body (LaunchPad 2) | node 10 ms (prio 3), display 200 ms (prio 1), watchdog 100 ms (prio 4) | Heartbeat 0x100, BodyStatus 0x201 | servo, lights (LED2), ST7735 LCD |

The watchdog task feeds the 2 s hardware watchdog only when every task has made progress, so a hung task resets the
board. Messages are defined in `can/fleetbus.dbc`; C code is generated from it.

## Branches to test in order

`main` has everything; each branch is the code exactly as it was when that step was finished.

| Branch | Step | Bench check (details) |
|---|---|---|
| `p2-bringup` | clock, UART log, FreeRTOS, flashing | `docs/mspm0-bench-steps.md` P2 |
| `p3-can` | CAN driver + loopback / two-node self-test | `docs/mspm0-bench-steps.md` P3 |
| `p4-sensors` | ADC, INA219, servo, lights | `docs/mspm0-bench-steps.md` P4 |
| `p5-node-roles` | battery + body apps (slot images, started from GDB) | `docs/mspm0-bench-steps.md` P5 |
| `l1-lite-images` | the same apps as standalone Lite images at 0x0 | L1 below |
| `l2-lite-dash` | live web dashboard | L2 below |
| `l3-lcd-gauge` | LCD instrument cluster on the body node | L3 below |
| `l4-lite-docs` | this page | — |

## Parts

2× LP-MSPM0G3507, 2× 3.3 V CAN transceiver (SN65HVD230 or TCAN1042), 2× 120 Ω, twisted pair, 10 kΩ pot,
10 kΩ NTC + 10 kΩ resistor, INA219 breakout, hobby servo + 5 V supply, Adafruit 1.8" TFT breakout (product 358, ST7735R, 160×128),
jumper wires. Optional: a USB-CAN adapter (CANable) for the terminal and web dashboards. Pins:
`firmware/bsp/mspm0g3507/bsp_pins.h`.

## L1: Lite images

```bash
make fw-mspm0
tools/flash_mspm0.sh lite battery     # LaunchPad 1 (writes lite_battery at 0x0; no bootloader)
tools/flash_mspm0.sh lite body        # LaunchPad 2
```

1. Console (115200): `node 1 fw v1 lite (no bootloader)` then one status line per second with `st`, `peer`, `can_err`,
   `stk_free` (and `disp_ms` on the body node).
2. Body boots in LIMP_HOME and leaves it 1 s after it first hears a healthy battery (fail-safe start, by design).
3. Turn the pot below 3.30 V: battery WARNING; below 3.00 V: FAULT then SAFE, body LIMP_HOME. Pull the battery's
   CAN wires: body LIMP_HOME after the 500 ms heartbeat timeout. Record both reaction times.
4. Watchdog: these images start the 2 s watchdog themselves. Halting a task in GDB does not test it (the WWDT pauses
   while the CPU is halted); the P8/OTA steps do.

## L2: live web dashboard

```bash
make lite-dash                                        # simulator: real firmware logic, slider + "unplug battery" switch
tools/lite_dash.py --can slcan:/dev/tty.usbmodemXXXX  # the bench bus through the CANable (receive only)
```

Open http://127.0.0.1:8000. Check against the bench: gauges follow the pot, thermistor and INA219; node cards show
NORMAL/WARNING/FAULT/SAFE/LIMP_HOME and go SILENT 500 ms after a node stops; `0 decode errors`. For a one-board test,
the CANable acknowledges frames so the single node does not error out.

## L3: LCD instrument cluster

Wire the Adafruit 1.8" TFT breakout (ST7735R) to the body LaunchPad:

| Breakout pin | LaunchPad |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCK | PB9 (SPI1 clock) |
| MOSI | PB8 (SPI1 data) |
| TFT_CS | PB6 |
| D/C | PB16 |
| RESET | PB15 |
| LITE | 3V3 (backlight PWM input; tie high for full brightness) |
| MISO, CARD_CS | not connected (the microSD slot is unused) |

Flash `lite body` (or any body image built for the MSPM0).

1. The screen shows the cluster (`docs/img/lcd_normal.png` is the real renderer output). With no battery on the
   bus: NO DATA, then BATTERY SILENT once a heartbeat has been seen and stops.
2. **Panel tab.** Adafruit has shipped this breakout with different panels ("black tab" / "green tab", after the tab
   on the protective film), and the label is not always reliable. The default build assumes black tab. If red and blue
   are swapped (the NORMAL banner shows blue instead of green), or there is a 1–2 px band of junk on one edge, rebuild
   with `cmake -S firmware -B build/fw-mspm0 -DFB_LCD_GREENTAB=ON ...` and reflash. If it is mirrored or rotated, change
   `LCD_MADCTL` in `firmware/bsp/mspm0g3507/bsp_display.c`. Record which setting your panel needs.
3. **The RTOS measurement:** `disp_ms` on the status line is one frame's draw time. Then check the node task kept its
   10 ms period while the display draws: heartbeat spacing on the CANable (`candump -t d` on Linux or the web
   dashboard's frame list) should stay 100 ms ± a tick. Record draw time and heartbeat jitter.
4. Turn the pot and pull the battery's CAN wires: the needle, values and banner follow within ~200 ms (one display
   period).

## Dashboards

Four ways to watch the bus, chosen in `docs/lite-dashboards.md`: terminal (`cantools monitor`), the web page (L2),
the full backend dashboard (`gateway --can`, heavier), and the LCD (L3). Mock-ups and the real LCD renderer output are
on the 3D bench page.
