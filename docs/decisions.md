# Design decisions

Each one: what was chosen, what it was compared against, and when to revisit. Numbers are TARGETs until they appear in
`docs/measured.md`.

## D1: Two LaunchPads on classic CAN, not CAN FD or a single board

The MSPM0G3507 has an MCAN controller (FD-capable). Classic CAN 2.0A at 500 kbit/s: it is what body and powertrain
buses in most cars run, every tool (CANable, PulseView, WaveForms) decodes it, and 8-byte frames fit every message.
Two boards, because the interesting failures (a node going silent, arbitration, bus-off) need a second node.
**Revisit if** a message needs more than 8 bytes.

## D2: The DBC is the single source of truth

`can/fleetbus.dbc` defines every message. C pack/unpack is generated from it (`tools/dbc2c.py`, cantools), and the
laptop decodes with the same file. Rejected: hand-written structs on each side (they drift; the generated code also
range-checks signals). Cost: the generator emits double-precision helpers the firmware never calls; `--gc-sections`
drops them (checked with `nm`).

## D3: Heartbeat id = 0x100 + node id

Each node's heartbeat has its own id (0x101, 0x102). With one shared 0x100, two nodes that start a heartbeat in the
same bit time both win arbitration, because the ids are identical, then collide on the first data byte (NodeId): bit
error, error frame, both retry at once, the same collision again. Arbitration only resolves frames with different ids.
Found in review before any hardware ran (2026-11-23).

## D4: Peer timeout 500 ms, recovery hold 1 s

Heartbeats are 10 Hz. 500 ms = 5 missed in a row: rides out a few lost frames or one error-passive episode, short
enough that the body reacts in about half a second. Leaving limp-home needs 1 s of continuous health, so a flapping
link can't toggle the lights. Silence counts as a fault: a dead battery ECU can't report its own failure. Both are
TARGETs: the bench measures the real detection time (`docs/measured.md`).

## D5: FreeRTOS tasks in rate-monotonic order, watchdog on top

| Task | Prio | Period | Why that priority |
|---|---|---|---|
| wdg | 4 | 100 ms | decides, does no work; must not be starved by a runaway lower task |
| node | 3 | 10 ms | the CAN deadline: drains RX, runs the FSMs, transmits |
| sense | 2 | 20 ms | battery only: ADC reads, faster than the 100 ms status rate it feeds |
| disp | 1 | 200 ms | body only: SPI redraw can take milliseconds; it must never delay the node task |
| log | 1 | 1 s | polled UART, ~7 ms per status line: kept out of the node task for the same reason |

Rejected: one super-loop (the display draw would delay CAN), and making the node task interrupt-driven (polling every
10 ms with a 14-frame RX FIFO is enough at this bus load, and keeps all CAN handling in one task). The node task records
its own worst-case execution time and start lateness (`node_us_max`, `late_us_max` on the console) so the display's
effect is measured, not argued.

## D6: Tasks share data through snapshots, not queues or shared structs

`Latest<T>` (`app_main.cpp`): the writer overwrites, readers copy, inside a critical section of a few dozen bytes.
Readers always want the newest sensor sample or display state, not a backlog, so a queue would be the wrong tool. No
mutexes, so no priority inversion to reason about: the only shared-resource lock is "interrupts off for a struct copy".

## D7: The simulator runs the real logic

`firmware/host/fbsim.cpp` wraps the same C++ node classes the board runs; the dashboard's `--sim` mode and the tests
drive them. Rejected: a Python model of the nodes, which would test itself. Cost: Python needs `make host-test` first.

## D8: Watchdog fed only when every task checks in

The 2 s WWDT is started in `main()` and can't be stopped. The wdg task feeds it only if the node task (and sense on the
battery, disp on the body) made progress since its last look. A hung task resets the board within about 2 s instead of
leaving a node that heartbeats but doesn't work.

## D9: TI DriverLib and CMake, not Code Composer Studio projects

The board layer calls TI's DriverLib directly; a vendored SDK subset (`tools/vendor_mspm0_sdk.sh`) and CMake build it
headless on macOS and in CI. CCS is used only for its `DSLite` flasher. Rejected: SysConfig-generated init code (opaque
diffs) and Zephyr (hides the RTOS and driver work this project is meant to show).

## D10: Scope cut (2026-11-23)

The project first grew a bootloader, signed OTA, a UDP gateway, a Go/Postgres backend, a React dashboard and an in-car
OBD-II node, all verified only in simulation. Cut to Lite before bring-up so the repo matches what runs on the bench.
The full version is preserved on the `full-platform` branch.
