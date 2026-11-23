# MSPM0 bench steps, one milestone at a time

How to test each port milestone on real LaunchPads. Each milestone has a branch (`p1-board-split` … `p8-docs-ci`) that holds the code exactly as it was when that milestone was finished; `main` has all of them. Work through the branches in order: check out the branch, build, flash, run the checks, and write what you measured into `docs/measured.md` (date, board serial if visible, numbers, scope screenshot paths).

**Doing FleetBus Lite first?** Follow P2–P5 here, then the Lite steps L1–L3 in [`docs/lite.md`](lite.md).

Nothing in this file has been done yet. Every expected value is from TI documents or an estimate, and says so.

Common setup:

```bash
git checkout p2-bringup           # or the milestone you are testing
make setup host-test fw-mspm0     # builds into build/fw-mspm0/arm/mspm0g3507/
```

Serial console: the LaunchPad's XDS110 enumerates two USB serial ports; the "Application/User UART" one is UART0 (PA10/PA11). Open it at 115200 8N1 (`screen /dev/tty.usbmodemXXXX 115200`). Jumpers J101 must be fitted for the backchannel (TI SDK note).

## P1 board split

Nothing to flash. Checks are on the computer:

- `make host-test`: both board maps pass (85 tests each when P1 landed).
- `make fw`: STM32 build still links. Bootloader about 17.6 KB of 28 KB.

## P2 bring-up (blink + log)

Goal: prove the clock tree, GPIO, UART and FreeRTOS on the M0+, and close P0 risk R6 (headless flashing).

1. **Flash path (R6).** Try `tools/flash_mspm0.sh bench bringup`. It uses DSLite from Code Composer Studio by default (the target file is the MSPM0G3507 one CCS ships); Homebrew's OpenOCD 0.12.0 has no MSPM0 target. Record which one worked and the exact command in `docs/toolchain.md`. If neither works headlessly, flash `build/fw-mspm0/arm/mspm0g3507/bringup.elf` from the UniFlash GUI once and note it.
2. **Console.** Expect `FleetBus bring-up on mspm0g3507` and then one `bringup ... t=<ms> n=<count> free_heap=<bytes>` line per second. Garbage characters mean the UART clock assumption (ULPCLK = 40 MHz) is wrong: see `bsp/mspm0g3507/bsp_core.c`.
3. **LED.** LED1 (red, PA0) toggles every 250 ms. Measure the period on a scope or logic analyser: expect 500.0 ms. Off by 2.5x means the CPU is still on the 32 MHz SYSOSC (SYSPLL never took over).
4. **Tick accuracy.** Let it run 10 minutes; compare `t=` with a stopwatch. SYSOSC-derived, so expect within about 1 % (EST; datasheet SYSOSC accuracy applies).
5. **MCLK.** Optional: TI's `sysctl_mclk_syspll` example drives CLK_OUT; not wired in our build. A scope on the LED is enough here.
6. **Debugger.** Attach GDB (OpenOCD `gdb_port 3333`, `arm-none-eabi-gdb build/fw-mspm0/arm/mspm0g3507/bringup.elf`, `target extended-remote :3333`), set a breakpoint on `log_task`, continue. This closes the "GDB from the CLI" half of R6.

Write down: flasher used, LED period, 10-minute tick drift, free heap printed.

## P3 CAN on MCAN

Goal: classic CAN at 500 kbit/s between two LaunchPads and the CANable, with the bit timing measured.

Wiring per node: LaunchPad PA12 (CAN_TX) to transceiver TXD, PA13 (CAN_RX) to RXD, 3.3 V and GND to the transceiver. Transceivers' CANH/CANL to a twisted pair. 120 Ω at each end of the pair only. With everything unpowered, measure CANH to CANL: expect about 60 Ω.

**No transceivers? Diode wired-AND bus (bench only).** Each board: a Schottky diode (BAT54, BAT43, 1N5817) from the bus
line to PA12 (CAN_TX), cathode (band) toward PA12. PA13 (CAN_RX) straight to the bus line. One 1 kΩ resistor from the
bus line to 3.3 V. GND of both boards joined. Low = dominant, high = recessive: any TX driving low pulls the bus low
through its diode, and every RX reads the bus. No termination, no CANH/CANL, so the CANable cannot join this bus; use
the logic analyser (WaveForms, CAN decoder, 500 kbit/s, on the bus line) as the third witness in step 4. Keep wires
short (under about 30 cm). The firmware is the same as with transceivers.

1. **Loopback.** Flash `tools/flash_mspm0.sh bench can_selftest` on one board with nothing on the bus. Expect `CAN loopback: 100/100 PASS`. A FAIL here is the driver, not the wiring.
2. **Bit timing.** Same board, transceiver connected, scope on CANH-CANL (or the TX pin). The node sends id 0x7F0 every 100 ms. Measure one bit time: expect 2.000 µs (500 kbit/s). With nobody to ACK, the frame repeats back-to-back (error frames): that is expected on a one-node bus.
3. **Two nodes.** Flash the second LaunchPad with the same image and join the buses. Each console must print `rx id=7f0 ... n=<the other node's counter>` ten times a second, and `can_err` must stay flat.
4. **CANable.** Add the CANable (`sudo ip link set can0 up type can bitrate 500000` on Linux, or `slcand` on macOS). `candump can0` shows 0x7F0 from both nodes at 10 Hz each.
5. **Sample point.** Optional: TI's nominal setting samples at 87.5 %, the STM32 build at 80 %. If you mix a Nucleo onto this bus later, note it; both are normal for 500 kbit/s.

Never connect this image to the car: it transmits a non-OBD id ten times a second.

Write down: loopback result, measured bit time, two-node error count after 10 minutes.

## P4 sensors and actuators

Goal: every I/O the node roles use works on the LaunchPad pins in `firmware/bsp/mspm0g3507/bsp_pins.h`. First, check each pin in that file against the LaunchPad schematic and header silkscreen; fix the header before wiring if anything differs.

Wiring: 10 kΩ NTC + 10 kΩ divider into PA25 (ADC ch1, "thermistor"), a 10 kΩ pot between 3.3 V and GND with the wiper on PA24 (ch2, "pack voltage"), INA219 breakout on PB2 (SCL) / PB3 (SDA) with 3.3 V power, servo signal on PB4 (servo powered from a separate 5 V supply, grounds joined), LED2 on the LaunchPad is the head/turn light (PB27 green, PB22 blue).

1. Flash `tools/flash_mspm0.sh bench io_selftest`. Expect `adc 0 i2c 0` on the first line and a status line every 500 ms.
2. **ADC.** Sweep the pot end to end: ch2 should read about 0 to 3300 mV. Compare three points with a multimeter on PA24 and record the error. Hold the thermistor: ch1 should move.
3. **INA219.** With nothing through the shunt, expect about 0 mA. Put a known load (for example a 100 Ω resistor from 5 V) through the shunt and compare with a multimeter in series. `NO-ACK` means the I2C wiring, address (0x40) or pull-ups are wrong.
4. **Servo.** Scope PB4: period 20.0 ms, high time stepping 1.00 / 1.25 / 1.50 / 1.75 / 2.00 ms. If high and low are swapped, the PWM polarity assumption (high time = period − compare) is wrong: fix `bsp_servo_set_deg`.
5. **Lights.** LED2 cycles off, green, blue, (LED1 red), green+blue. Swap pins in `bsp_pins.h` if the colours are mapped differently on your board.

Write down: ADC error at three points, INA219 vs multimeter, measured servo pulse widths.

## P5 node roles under FreeRTOS

Goal: the battery and body roles run on two LaunchPads and talk over CAN exactly as the simulator does. The bootloader arrives in P6, so here the app is started by hand from GDB.

Build: `cmake -S firmware -B build/fw-mspm0 -G Ninja -DFB_BOARD=mspm0g3507 -DCMAKE_TOOLCHAIN_FILE=$PWD/firmware/cmake/arm-none-eabi.cmake && cmake --build build/fw-mspm0`. App sizes when P5 landed: battery 29.3 KB, body 28.9 KB, telematics 28.6 KB of the 48.6 KB slot; 15 KB of 32 KB SRAM.

1. **Flash an app into slot A** (no bootloader yet): `tools/flash_mspm0.sh app battery A 1` on node 1, `tools/flash_mspm0.sh app body A 1` on node 2. This signs for the MSPM0 board and writes at 0x8000.
2. **Start it from GDB** on each board (OpenOCD running):
   ```
   arm-none-eabi-gdb build/fw-mspm0/arm/mspm0g3507/app_battery_A.elf
   (gdb) target extended-remote :3333
   (gdb) monitor reset halt
   (gdb) set *(unsigned int *)0xE000ED08 = 0x8200
   (gdb) set $sp = *(unsigned int *)0x8200
   (gdb) jump *(*(unsigned int *)0x8204)
   ```
   The watchdog is not running without the bootloader (the app skips feeding an unpowered WWDT).
3. **Console.** Each node prints `t=... st=1 peer=1 can_err=0 tx_drop=0 stk_free=...` once a second. `st=1` is NORMAL. `peer=1` means it sees the other node's heartbeat.
4. **Fault path.** Turn the pot (pack voltage) below the warning and fault thresholds: the battery node goes WARNING (2) then FAULT (3), and the body node goes LIMP_HOME (5): servo stops, lights change. Unplug the battery node's CAN: the body node goes LIMP_HOME after the heartbeat timeout.
5. **CANable.** `candump can0` shows the heartbeat 0x100 from both nodes (node id in the first byte), BatteryStatus 0x200 and BodyStatus 0x201; decode with `cantools decode can/fleetbus.dbc`.
6. **Stack and heap.** Record `stk_free` after 10 minutes and after a CAN storm (`cangen can0 -g 1`). The node task has 4 KB; anything under 512 B free needs a bigger stack. (P5 found the old 1.5 KB stack would overflow during OTA verify on both boards: Ed25519 alone peaks at 1,664 B on M4 and 1,888 B on M0+, measured in emulation.)

Write down: heartbeat period on candump, fault and limp-home transitions, `stk_free` minimum.

## P6 bootloader and flash

Goal: the real boot chain on one LaunchPad: bootloader verifies the signed image, boots it as TRIAL, the app confirms, power cuts never brick it.

1. **Provision a fresh board.** `tools/flash_mspm0.sh provision battery 1` flashes `bootloader_battery` at 0x0, signs and writes `app_battery_A` v1 at 0x8000, and writes the metadata record that marks slot A NEW (`tools/provision_meta.py`). Without that record the bootloader sees both slots EMPTY and only blinks LED1 fast (100 ms). That is the correct, fail-closed behaviour, not a bug.
2. **Boot time.** Scope LED1 / a GPIO from reset release to the app banner `node 1 fw v1 slot A TRIAL`. P0 estimated 0.43 to 0.65 s for one image verify at 80 MHz; the whole bootloader should be under about 1 s. Record the number.
3. **Confirm.** After the 10 s health window the app confirms; reboot (reset button) and the banner says `confirmed`.
4. **Watchdog continuity (P0 R4).** With the confirmed image running, short-circuit the feed: in GDB, `break wdg_task` and leave the target halted is not a test (the WWDT stops while the CPU is halted for debug, TRM ch. 40). Instead run the bench `_unhealthy` image through OTA in P7: the watchdog started by the bootloader keeps running in the app, the app stops feeding when its health deadline passes, and the board resets within 2 s. That is the R4 proof.
5. **Erased-flash reads.** A fresh board must not reset-loop. Erase the whole chip, flash only the bootloader, power on: LED1 blinks fast (nothing bootable), no reset loop. This is the P0 nondeterministic-erase finding: reads go through the ECC-uncorrected alias.
6. **Power cuts.** Power the LaunchPad through a relay (remove the XDS110 3V3 jumper so USB does not back-power it), then `tools/powercut.py --port <uart> --on "<relay on>" --off "<relay off>" --cycles 200`. It cuts power at random points in the first 1.5 s after power-on, while the bootloader updates the attempt counter. Expect 200/200.

Bootloader size when P6 landed: 18,080 B of 28 KB (measured link). Write down: boot time, cycles passed in the power-cut test, reset count in step 4.

## P7 OTA end to end

Goal: a signed update travels laptop → backend → gateway → CAN → LaunchPad, installs, is health-checked and confirmed; a deliberately bad image is rolled back on its own. Same code path the simulator proves in `tests/e2e` (also run against the MSPM0 flash map with `FB_BOARD=mspm0g3507` when P7 landed: see `docs/verification.md`).

Setup: two provisioned, confirmed nodes from P6 (battery v1, body v1) on the P3 bus with the CANable. Start the backend with the **dev** public key (the one `firmware/keys/fleetbus_pub.h` was generated from), not the simulator key that `scripts/demo.sh` uses:

```bash
make db && (cd backend && go build -o ../build/bin/ ./cmd/...)
DSN="postgres://fleetbus:fleetbus@localhost:5433/fleetbus?sslmode=disable"
PUB=$(python3 -c "import re;print(''.join(re.findall(r'0x([0-9a-f]{2})',open('firmware/keys/fleetbus_pub.h').read())))")
FLEETBUS_DSN=$DSN build/bin/ingest -listen 127.0.0.1:9000 &
FLEETBUS_DSN=$DSN build/bin/api -listen 127.0.0.1:8080 -static dashboard/dist -pubkey $PUB &
.venv/bin/python -m fleetbus.gateway --can slcan:/dev/tty.usbmodemXXXX --id 1 --backend 127.0.0.1:9000
```

1. **Good update.** `make fw-mspm0 && tools/sign_release.sh mspm0g3507 2`, then `build/bin/fbctl image upload -a build/release/mspm0g3507/v2/app_body_A.signed -b build/release/mspm0g3507/v2/app_body_B.signed -notes "bench v2"` and `fbctl rollout start -image <ID> -window 30s -soak 3s`. Watch `fbctl rollout status <ID> -watch` and the node console: transfer, `DONE`, reboot, banner `slot B TRIAL`, then confirmed. Dashboard on :8080 shows the version change.
   Measure: transfer time for the ~29 KB image (P5 size) and the time from reboot to confirm.
2. **Unhealthy image.** Build with bench fault images (`cmake ... -DFB_BENCH_FAULT_IMAGES=ON`), `tools/sign_release.sh mspm0g3507 3 _unhealthy`, upload the `_unhealthy` pair, start a rollout. The node boots v3 as TRIAL, never reports healthy, stops feeding the watchdog at its 10 s deadline, the WWDT resets it (this is the P0 R4 proof: the bootloader started it, the app inherited it), and after 3 attempts the bootloader marks the slot BAD and boots v2 again. The rollout engine halts (fail-closed). Measure rollback time from the first v3 boot to the v2 banner, and write it in `docs/measured.md`.
3. **Hang image.** Same with the `_hang` variant (hangs before starting the scheduler): rollback through the watchdog without the app ever running.
4. **Wrong board.** Upload an STM32 pair (`tools/sign_release.sh stm32g4 4`) and push it to an MSPM0 node: the node rejects it at the end of the transfer (hw id), stays on its current version, the rollout fails closed.
5. **Power cut during transfer.** Cut node power halfway through a transfer: it comes back on the old image; the gateway and rollout engine retry or fail closed.

Write down: transfer time, reboot-to-confirm time, rollback time for unhealthy and hang images.

## P8 bench fault injection

Goal: measured behaviour when the bus is physically broken, with scope captures. Use the two-node `can_selftest` setup from P3: it now also prints every error-state transition (`can error-active -> BUS-OFF after N ms (tec=.. rec=..)`), sampled every 5 ms.

1. **Short CANH to CANL** for about 1 s with a jumper. Both nodes go error-passive then bus-off; remove the short and each must recover on its own (`BUS-OFF -> error-active after N ms`). Expected recovery: 128 × 11 recessive bits ≈ 2.8 ms at 500 kbit/s plus up to one 100 ms send poll (the driver leaves bus-off on the next send/receive). Scope CANH/CANL during the event and save the capture.
2. **Remove one terminator.** Usually still works on a short bench bus; record the waveform ringing and whether error counters climb.
3. **Remove both terminators.** Expect errors and error-passive; record.
4. **Bitrate mismatch.** Bring the CANable up at 250 kbit/s on the same bus. The nodes see errors and may go error-passive; nothing should hang. Restore 500 kbit/s and confirm recovery.
5. **Node roles under faults.** Repeat step 1 with the P5 node apps running: the body node must enter limp-home while the bus is down (no heartbeat), and leave it when the battery node is back.

Then the remaining P8 items, all off-bench: `make test`, `make e2e` and `FB_BOARD=mspm0g3507 make e2e` green; CI green on GitHub (both boards build); every row you measured moved from TARGET/ESTIMATED to measured in `docs/measured.md`, `docs/verification.md` and the README.

Write down: recovery time per fault, scope captures (file names), limp-home enter/exit times.
