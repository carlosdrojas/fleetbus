# MSPM0G3507 port plan

Written 2026-10-12. **Decision (Carlos, 2026-10-12): the primary hardware is the TI LP-MSPM0G3507 LaunchPad** (the board from his embedded-systems class), replacing the Nucleo-G431RB plan of 2026-10-03. The STM32 BSP stays in the tree as a secondary target and as the fallback if the Phase 0 spike fails.

Evidence labels: **T1** = read in TI documents (SLASEX6C datasheet rev Oct 2025, SLAU873-era LaunchPad user guide, SLAAET4 app note). **EST** = my estimate, to be measured. **VERIFY** = not confirmed, check before relying on it. Nothing here has run on a board.

## 1. Why this is mostly a BSP port

`firmware/bsp/bsp.h` is a thin board interface (clock, GPIO/LED, UART log, millis, watchdog, flash, CAN, ADC, I2C/INA219, servo, lights, telematics link). Everything above it is board-independent and already host-tested: `firmware/common/` (CAN messages, fault/body FSMs, boot logic and metadata, image verify, OTA receiver, OBD allowlist, UART link), `firmware/host/` simulator, the Python gateway, the Go backend, the dashboard and the HIL harness. The port touches:

| Area | STM32G431 today | MSPM0G3507 | Work |
|---|---|---|---|
| Core | Cortex-M4F, 170 MHz, FPU | Cortex-M0+, 80 MHz max, no FPU (T1) | New toolchain flags, FreeRTOS `ARM_CM0` port, drop FPU config |
| Flash | 128 KB, 2 KB pages, 64-bit program | 128 KB, 1 KB sectors, 64-bit flash word, ECC (T1) | New `bsp_flash.c`; make page size/map a per-board header |
| RAM | 32 KB (22 KB main + CCM) | 32 KB with parity option (T1) | Linker scripts: `RAM LENGTH = 22K` becomes 32K minus stack/heap budget |
| CAN | FDCAN1 classic 500 kbit/s | MCAN0, PA12 TX / PA13 RX (T1) | New `bsp_can.c` on TI DriverLib |
| Clock | HSI16 + PLL to 170 MHz | 40 MHz crystal (Y2) on the LaunchPad, up to 80 MHz via SYSPLL (T1) | New `bsp_clock.c` |
| Watchdog | IWDG started by the bootloader, fed by the app | Two windowed watchdogs (WWDT0/1) (T1) | **Design check**, see risk R4 |
| Sensors | ADC1, I2C1 INA219, TIM3 servo PWM, GPIO lights | ADC, I2C, TIMG PWM, GPIO | New `bsp_sensors.c` + pin map |
| UART | LPUART1 over ST-LINK VCP | UART0 over the XDS110 backchannel (PA10/PA11) (T1) | New `bsp_core.c` log/link |
| Vendor layer | `stm32g4xx_hal_driver`, `cmsis_device_g4` submodules | TI MSPM0 SDK (DriverLib + CMSIS device headers) | New third-party layer, new startup file |
| Debug/flash | ST-LINK | XDS110 on the LaunchPad | **Phase 0:** prove headless flash + GDB from CLI |

## 2. Risks that decide whether the port works (retire these first)

- **R1: bootloader size.** The M4 bootloader with Monocypher Ed25519 measured 24,504 B and was given 28 KB (decision D6). Cortex-M0+ (Thumb-1) code is usually larger and has no 64-bit multiply or hardware divide, so `libgcc` helpers get pulled in. **EST:** it may exceed 28 KB. Note `firmware/arm/app.ld.in` currently discards `libgcc.a`, which will break an M0+ link (needs `__aeabi_*` helpers); that discard must go.
- **R2: Ed25519 verify time.** M4 at 170 MHz vs M0+ at 80 MHz without 32x32 to 64-bit multiply. **EST:** several times slower, possibly seconds. `fb_boot_run` verifies slots on every reset, and the 2 s watchdog is armed after it. Measure first; if too slow, verify only the chosen slot and/or cache a verified flag in metadata.
- **R3: flash model.** A flash "word" is 64 bits with ECC (T1: tPROG(WORD,64) 50 to 275 us). The datasheet also lists "Write operations per word line before sector erase: 83 max" (T1). Check that `boot_meta` and the OTA writer program each word once between erases, and that nothing rewrites a word in place. Sector erase is 4 to 200 ms depending on wear (T1), below the 500 ms peer timeout in D6.
- **R4: watchdog continuity across bootloader to app.** The design arms IWDG in the bootloader and the app must keep feeding it. WWDT is windowed, with its own start/stop rules (T1 only says it can stop automatically in sleep). **VERIFY** in the TRM that it can be started by the bootloader and continue across the jump, and what feeding too early does. If not, the app starts it and the bootloader relies on a timeout-based trial counter.
- **R5: vector table relocation on M0+.** `jump_to_app` writes `SCB->VTOR`. **VERIFY** the MSPM0G implements VTOR (it is optional on Cortex-M0+). `NVIC->ICER[8]` becomes 1 register on M0+. Fallback: relocate vectors in RAM.
- **R6: toolchain and tooling.** TI distributes the SDK via installer; confirm license terms and how to vendor it (pinned copy or submodule/mirror). Confirm headless flashing and GDB over XDS110 (UniFlash/DSLite CLI or OpenOCD) because CI and this workflow are CLI-driven.
- **R7: FreeRTOS + MCAN.** TI's CAN examples are non-RTOS (T1, older SDK guide). The RTOS integration is ours. Low risk: `bsp_can_*` is poll/FIFO based today.
- **R8: no transceiver on the LaunchPad (T1).** CAN pins reach BoosterPack header pins 32 (TX) and 31 (RX) through 0 ohm R64/R65. Each node needs a 3.3 V transceiver (SN65HVD230 or TCAN1042) and 120 ohm termination at both bus ends.
- **R9: how many LaunchPads.** The design needs 3 nodes (battery, body, telematics). Confirm how many boards Carlos has; the third can wait.

If R1 or R2 fails the Phase 0 spike, the fallback is: (a) shrink the signature scheme cost (verify one slot per boot), (b) grow the bootloader to 32 KB and slots to 44 KB (code size EST), or (c) keep STM32 as the OTA-capable pair and make MSPM0 a non-OTA CAN node. Decide at the P0 gate, not later.

## 3. Milestones

Follows AGENTS.md workflow (one milestone per session; model per milestone). Gates are in bold.

| # | Milestone | Model | Done when |
|---|---|---|---|
| **P0** | **Feasibility spike** (1 session, no hardware needed for steps a to c). **Done 2026-10-12 except (d): see `docs/p0-feasibility.md`. R1 to R5 pass; R6 open until a board arrives** | Opus | (a) Cross-build the existing `boot_logic` + Monocypher for `-mcpu=cortex-m0plus` and record bootloader size vs 28 KB. (b) Time Ed25519 verify on a LaunchPad via a minimal firmware (or estimate from cycles if board unavailable, labeled EST). (c) Read TRM for VTOR, WWDT start/continue, flash write rules. (d) Prove CLI flash + GDB on XDS110. **Gate: R1, R2, R4, R5, R6 answered.** |
| P1 | Board abstraction | Sonnet | `bsp/` split into `bsp/stm32g4/` and `bsp/mspm0g3507/`; `flash_if.h` map (page size, boot size, meta, slot addresses) moves to a per-board header; CMake `-DBOARD=` option; STM32 build and all host tests still pass. Also the P0 fixes: real `-Os` and no `-fPIC` in the cross build, verify the chosen image once per boot, bootloader stack reserve ≥ 3 KB, fake flash option for nondeterministic erased reads |
| P2 | MSPM0 bring-up | Sonnet | SDK vendored, startup + linker scripts for M0+, FreeRTOS `ARM_CM0` port, clock, LED, UART log; "blink + log" runs under FreeRTOS on a LaunchPad |
| P3 | CAN on MCAN | Sonnet | `bsp_can_*` on MCAN0, 500 kbit/s classic CAN. Same bit timing as today: 40 MHz CANCLK, prescaler 4 gives 10 MHz time quantum, 20 tq per bit (1 + 15 + 4), 80 % sample point (EST; confirm clock source with the SLAAET4 "MCAN Clock Frequency" section and a scope). Loopback, then two nodes through transceivers and the CANable |
| P4 | Sensors and actuators | Sonnet | ADC, INA219 over I2C, servo PWM, lights, telematics UART link on the XDS110 backchannel; pin map checked against the LaunchPad schematic |
| P5 | Node roles under RTOS | Sonnet | Battery, body and telematics apps run: heartbeat, fault FSM, limp-home, host-side tests unchanged. Stack high-water and heap use measured and recorded |
| **P6** | **Bootloader + flash** | Opus | `bsp_flash.c` for 1 KB sectors; bootloader fits its budget; A/B select, trial boot and rollback work on hardware; power-loss test (pull power mid-write) leaves a bootable node. **Gate: R3.** |
| P7 | OTA end to end | Opus | Signed image sent over CAN from the gateway, installed, health-checked, and a deliberately bad image (hang / never healthy) rolled back. Measure rollback time and write it in `docs/measured.md` |
| P8 | Bench fault injection and docs | Sonnet | Bus-off injection (short CAN_H/CAN_L, remove termination, baud mismatch) with scope captures and measured recovery; CI builds both boards; `measured.md`, `verification.md`, `bench-guide.md`, `hardware/README.md`, README updated; TARGET/ESTIMATED labels dropped only where measured |

**Status 2026-10-12:** the software side of every milestone is built, committed on `main`, and marked by a branch per milestone (`p0-feasibility`, `p1-board-split`, `p2-bringup`, `p3-can`, `p4-sensors`, `p5-node-roles`, `p6-bootloader`, `p7-ota`, `p8-docs-ci`). Every "done when" item that needs a board is still open and is written up as a bench procedure in `docs/mspm0-bench-steps.md`; results go in `docs/measured.md`. One change from the table: the flash driver landed in P5 (the apps need it for metadata and OTA writes), so P6 covers the bootloader, the jump, provisioning and the power-cut test.

STM32 hardware stays an optional side task: if the Nucleos arrive, P1's board split lets them run the existing build as a second, independent proof of the same protocol.

## 4. Flash layout (MSPM0G3507, 128 KB, 1 KB sectors): starting point, not final

Keep the same shape as today so `boot_logic`, `image`, and the OTA tools change as little as possible. **EST** until P0 measures the bootloader:

| Region | Today (STM32) | Starting point (MSPM0) |
|---|---|---|
| Bootloader | 28 KB at 0x0800 0000 | 28 KB at 0x0000 0000 (grow to 32 KB if P0 requires it) |
| Metadata A / B | 2 KB each at 0x0800 7000 / 0x0800 7800 | 1 KB each (one sector per copy), same neighbors |
| Slot A / B | 48 KB each | 48 KB each, or 44 KB if the bootloader grows |

Addresses: confirm the MSPM0 flash base from the datasheet memory map before editing `flash_if.h` (the STM32 address 0x08000000 will not apply). The signed image header (0x200 bytes) and VTOR alignment rule (512-byte) carry over unless R5 changes them.

## 5. Docs and resume follow-ups (after P0 passes, not before)

- `AGENTS.md`: Decisions (done in this change), Status, "What this is".
- `docs/proposal.md`, `docs/decisions.md` (D6 flash layout), `docs/toolchain.md`, `docs/bench-guide.md`, `hardware/README.md`, `docs/system-map.html`, `docs/bench-model.html`: STM32/Nucleo wording and parts list.
- Parts: per node a 3.3 V CAN transceiver breakout, 120 ohm resistors, twisted pair, jumpers; INA219 breakout, thermistor/pot, servo, LEDs; a CANable adapter (already planned).
- `~/Documents/Dev/Apply/projects/fleetbus.md`: facts F1, F2 and Keywords say STM32; the master.tex bullet names STM32 and FreeRTOS. Change to MSPM0G3507 (Cortex-M0+) only after the decision is recorded and the framing still matches what is built. The file's "Done so far" is also stale: it says nothing was built, but the repo now has the simulated stack. Still nothing has run on hardware, so no number goes on the resume.
- The 2026-10-03 reason for choosing STM32 was recognizability. Round-1 research did not find a vendor named in Tesla/SpaceX intern postings, but I did not search for one specifically. Treat as a tradeoff the owner accepted, not a verified non-issue.
- Update `~/Documents/Dev/Apply/applications/_research/context-transfer.md` FleetBus row (AGENTS.md convention).

## 6. Sources

- TI MSPM0G350x datasheet SLASEX6C: https://www.ti.com/lit/gpn/mspm0g3507
- LP-MSPM0G3507 user guide: https://www.ti.com/lit/pdf/slau873
- TI app note SLAAET4 (MSPM0 CAN-FD, bus-off section): https://www.ti.com/document-viewer/lit/html/SLAAET4/GUID-D45FF31F-B0D4-4A2E-99D9-C0A816205854
- TRM and errata (SLAZ742) not yet read: do in P0.
