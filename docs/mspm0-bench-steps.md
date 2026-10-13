# MSPM0 bench steps, one milestone at a time

How to test each port milestone on real LaunchPads. Each milestone has a branch (`p1-board-split` … `p8-docs-ci`) that holds the code exactly as it was when that milestone was finished; `main` has all of them. Work through the branches in order: check out the branch, build, flash, run the checks, and write what you measured into `docs/measured.md` (date, board serial if visible, numbers, scope screenshot paths).

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

1. **Flash path (R6).** Try `tools/flash_mspm0.sh bench bringup`. It uses OpenOCD by default; if your OpenOCD has no MSPM0 support, use `FLASHER=dslite` with a UniFlash `.ccxml`. Record which one worked and the exact command in `docs/toolchain.md`. If neither works headlessly, flash `build/fw-mspm0/arm/mspm0g3507/bringup.elf` from the UniFlash GUI once and note it.
2. **Console.** Expect `FleetBus bring-up on mspm0g3507` and then one `bringup ... t=<ms> n=<count> free_heap=<bytes>` line per second. Garbage characters mean the UART clock assumption (ULPCLK = 40 MHz) is wrong: see `bsp/mspm0g3507/bsp_core.c`.
3. **LED.** LED1 (red, PA0) toggles every 250 ms. Measure the period on a scope or logic analyser: expect 500.0 ms. Off by 2.5x means the CPU is still on the 32 MHz SYSOSC (SYSPLL never took over).
4. **Tick accuracy.** Let it run 10 minutes; compare `t=` with a stopwatch. SYSOSC-derived, so expect within about 1 % (EST; datasheet SYSOSC accuracy applies).
5. **MCLK.** Optional: TI's `sysctl_mclk_syspll` example drives CLK_OUT; not wired in our build. A scope on the LED is enough here.
6. **Debugger.** Attach GDB (OpenOCD `gdb_port 3333`, `arm-none-eabi-gdb build/fw-mspm0/arm/mspm0g3507/bringup.elf`, `target extended-remote :3333`), set a breakpoint on `log_task`, continue. This closes the "GDB from the CLI" half of R6.

Write down: flasher used, LED period, 10-minute tick drift, free heap printed.

## P3 CAN on MCAN

Goal: classic CAN at 500 kbit/s between two LaunchPads and the CANable, with the bit timing measured.

Wiring per node: LaunchPad PA12 (CAN_TX) to transceiver TXD, PA13 (CAN_RX) to RXD, 3.3 V and GND to the transceiver. Transceivers' CANH/CANL to a twisted pair. 120 Ω at each end of the pair only. With everything unpowered, measure CANH to CANL: expect about 60 Ω.

1. **Loopback.** Flash `tools/flash_mspm0.sh bench can_selftest` on one board with nothing on the bus. Expect `CAN loopback: 100/100 PASS`. A FAIL here is the driver, not the wiring.
2. **Bit timing.** Same board, transceiver connected, scope on CANH-CANL (or the TX pin). The node sends id 0x7F0 every 100 ms. Measure one bit time: expect 2.000 µs (500 kbit/s). With nobody to ACK, the frame repeats back-to-back (error frames): that is expected on a one-node bus.
3. **Two nodes.** Flash the second LaunchPad with the same image and join the buses. Each console must print `rx id=7f0 ... n=<the other node's counter>` ten times a second, and `can_err` must stay flat.
4. **CANable.** Add the CANable (`sudo ip link set can0 up type can bitrate 500000` on Linux, or `slcand` on macOS). `candump can0` shows 0x7F0 from both nodes at 10 Hz each.
5. **Sample point.** Optional: TI's nominal setting samples at 87.5 %, the STM32 build at 80 %. If you mix a Nucleo onto this bus later, note it; both are normal for 500 kbit/s.

Never connect this image to the car: it transmits a non-OBD id ten times a second.

Write down: loopback result, measured bit time, two-node error count after 10 minutes.
