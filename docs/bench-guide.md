# Bench guide: what Carlos does when the parts arrive

Everything here is **[BENCH]**: it needs hardware, and nothing in it has been done yet. Software side is already verified
(`docs/verification.md`); this list closes the gap. Record every real number you measure in `docs/measured.md` (create it) and
only then upgrade a TARGET/ESTIMATED label.

## 0. Toolchain (once)
`make setup && make db && make test` should be green. `brew install --cask gcc-arm-embedded` (needs sudo) and `brew install stlink`.

## 1. Wire the bench "car"
Pin map (from the datasheet/UM2505, **verify against the board schematic before powering**):

| Function | Pin | Notes |
|---|---|---|
| FDCAN1 RX / TX | PA11 / PA12 (AF9) | to SN65HVD230 RXD / TXD, 3.3 V transceiver |
| LPUART1 (ST-LINK VCP) | PA2 / PA3 | `bsp_log` on battery/body; the serial **link** on the telematics node |
| I²C1 | PB8 / PB9 | INA219 (0x40), 0.1 Ω shunt; external pull-ups |
| ADC1 IN1 / IN2 | PA0 / PA1 | NTC divider (10 k + 10 k) / potentiometer standing in for pack voltage |
| Servo PWM | PB4 (TIM3_CH1) | **own 5 V supply**, common ground |
| Head / turn lights, fault LED | PB0 / PB1, PA5 (LD2) | |

CAN: twisted pair, **120 Ω at each end** (CANable's jumper is the far end). Use the logic analyzer on CAN and I²C.

## 2. First light (one board)
1. `make fw`, then `tools/flash.sh boot battery`, then `tools/flash.sh app battery A 1`.
2. Serial console (`screen /dev/tty.usbmodem* 115200`): expect `node 1 fw v1 slot A confirmed`, then a status line per second.
3. LD2 follows the fault state. Turn the pot below ~3.3 V-equivalent: WARNING, then FAULT (thresholds are TARGET placeholders).
4. **Measure**: bootloader time to first app instruction (Ed25519 + SHA-512 at 170 MHz; TARGET < 100 ms + hash) with a GPIO toggle on a scope.

## 3. Bus up (two boards + CANable)
1. `tools/flash.sh boot body && tools/flash.sh app body A 1` on the second board.
2. **Check bit timing** on the analyzer: 500 kbit/s, ~80 % sample point. If frames error, try 250 kbit/s first to separate wiring from timing.
3. `candump -L can0 > hil/traces/bench_idle.log` (or `python -m can.logger`). Both nodes' heartbeats at ~10 Hz.
4. Unplug the battery node's CAN wire: body must go to `LIMP_HOME` ≈ 500 ms later.
5. `HIL_CAN=slcan:/dev/tty.usbmodemXXXX .venv/bin/python -m pytest hil -q -s` (replays traces into the board; **bench only**, the adapter impersonates the battery node).

## 4. Fleet path
`scripts/demo.sh up 1` then stop its simulated car 1 gateway and run the real one:
`cd gateway && ../.venv/bin/python -m fleetbus.gateway --can slcan:/dev/tty.usbmodemXXXX --id 1 --backend 127.0.0.1:9000`.
Both bench nodes appear in the dashboard **without** the SIMULATED badge.

## 5. Real OTA (bench only; fault images never go near the car)
1. `make fw` (add `-DFB_BENCH_FAULT_IMAGES=ON` for the deliberately broken builds), then sign for both slots:
   `tools/sign_image.py sign build/fw/arm/stm32g4/app_body_A.elf.bin --node body --slot 0 --version 2` and the `_B` build with `--slot 1`.
2. `build/bin/fbctl image upload -a ... -b ...` then `build/bin/fbctl rollout start -image N`.
3. Expected: transfer, reboot into slot B in TRIAL, confirm within 10 s, rollout healthy. Then repeat with the `_hang` and `_unhealthy` images: the bootloader must return to the old version after 3 watchdog resets / 10 s, and the rollout must halt.
4. **Measure**: OTA duration for a ~38 KB image, CAN bus load during it, the node's heartbeat gap during each page erase (design says < 500 ms).
5. Pull power during a transfer, and during the first seconds after the switch. The board must come back on the previous good slot.

## 6. Day-one Sentra test (before any in-car work)
Car parked, ignition on, **passenger operates**, nothing mounted on the windshield.
1. CANable on the OBD-II cable (CAN-H pin 6, CAN-L pin 14), `tools/obd_listen.py slcan:/dev/tty.usbmodemXXXX --seconds 60 --out hil/traces/sentra_idle.log`. The script refuses to attach unless the driver confirms listen-only; if your adapter cannot, read its docs, then re-run with `--allow-software-only` and **confirm on a scope that the adapter never drives the bus**.
2. Expect either a stream of frames (passive sniffing works) or silence (the gateway module filters the port; then only OBD-II polling works). Record which.
3. Note the engine-off and engine-running battery voltage at the OBD port: set `FB_OBD_ENGINE_ON_MV` (TARGET 13000) from what you see.

## 7. In-car node (phase 4b)
Telematics node on 12 V through a USB buck, USB cable to the Pi, `gateway --serial /dev/ttyACM0` on the Pi (phone hotspot for the uplink). Flash with `tools/flash.sh boot telematics` / `app telematics A 1`.
The OBD-II port stays powered with the car off: **unplug the node when parked** (real low-power sleep is not implemented).
