# Measured on hardware

Numbers measured on the real boards go here first, one row per measurement, with the date, the board and the method.
Until a row has a value, the matching claim anywhere else stays labelled TARGET, ESTIMATE or "in simulation".
Procedure for each row: `docs/mspm0-bench-steps.md`.

P2 bring-up passed on both boards on 2026-11-24.

| Step | Quantity | Expected (source) | Measured | Date, board, method |
|---|---|---|---|---|
| P2 | Flash with DSLite from the command line | works (unproven) | works, after the 8-byte flash-block linker fix (first try failed verify at 0x1600) | 2026-11-24, A (XDS MG350001) and B (MG35000B), `XDS_SERIAL=… tools/flash_mspm0.sh bench bringup` |
| P2 | Bring-up console, both boards | banner + one line per second | one line per second, `t` steps 1000 ms, `free_heap=9944` steady (A to n=268, B to n=255) | 2026-11-24, A and B, console read at 115200 |
| P2 | LED1 period, bring-up image | 500.0 ms (250 ms toggle) | | |
| P2 | Tick drift over 10 min | within ~1 % (ESTIMATE, SYSOSC spec) | | |
| P3 | CAN loopback self-test | 100/100 | | |
| P3 | CAN bit time on the bus | 2.000 µs (500 kbit/s, arithmetic) | | |
| P3 | Diode-bus rise time, 10–90 % | well under 2 µs (1 kΩ pull-up, short wires) | | |
| P3 | Two-node `can_err` after 10 min | 0 | | |
| P4 | ADC error at 3 points vs multimeter | | | |
| P4 | Servo pulse widths 0 / 90 / 180° | 1.00 / 1.50 / 2.00 ms at 20 ms | | |
| P5 | Heartbeat period, 0x101 and 0x102 | 100 ms | | |
| P5 | Battery WARNING / FAULT "pack" voltage on the pot | 3.30 V / 3.00 V (firmware defaults) | | |
| P5 | Node task worst-case execution (`node_us_max`) | a few hundred µs (ESTIMATE) | | |
| P5 | Node task worst start lateness (`late_us_max`) | < 1000 µs | | |
| P5 | Minimum free stack per task after 10 min (`stk`) | > ~200 B each | | |
| L3 | Display frame time (`disp_us`), typical / worst / first full frame | unknown | | |
| L3 | Body `node_us_max` / `late_us_max` with the display vs battery without | same within tick resolution (TARGET) | | |
| L3 | 0x102 heartbeat spacing while the display redraws | 100 ms ± 1 ms (TARGET) | | |
| L3 | Panel tab that gives correct colours and edges | black tab (default) or `FB_LCD_GREENTAB` | | |
| F1 | Battery dies → body limp-home (last 0x101 frame → body PA0) | 500–520 ms (timeout + polling) | | |
| F2 | Battery back → body leaves limp-home (first 0x101 → PA0) | 1000–1020 ms (1 s hold) | | |
| F3 | FAULT heartbeat on the bus → body acts (message-to-action latency) | < ~10 ms (10 ms poll) | | |
| F4 | Bus stuck dominant ~1 s: bus-off, then recovery after release | recovery ≈ 2.8 ms + one poll (128 × 11 bits) | | |
| F5 | Bus open (one node alone): TEC behaviour | error-passive, not bus-off | | |
| F6 | Bus load | 40 frames/s ≈ 0.9 % of 500 kbit/s (arithmetic) | | |

## Bugs found on hardware

For each: symptom, how it was found, root cause, fix, commit. This becomes the failure story.

1. **Flash verify failed at 0x1600 (2026-11-24, both boards).** DSLite: "Length of block is 4, but it should be divisible
   by 8 since Flash Programmer writes in 64-bits". The `.data` load image was 4 bytes; the MSPM0 flash programs 64-bit
   words. Host tests can't see it. Fix: pad every flash load block to 8 bytes in `common.ld` (commit f39ff57).

## Bugs found before hardware

Found by review or host tests while preparing for the bench. Good context, but not hardware stories.

| Bug | Found by | Fix |
|---|---|---|
| Release builds were `-O3`, not `-Os`: CMake appends its own `-O3 -DNDEBUG` after the toolchain's flags | reading the build line | override `CMAKE_*_FLAGS_RELEASE` (`firmware/CMakeLists.txt`) |
| Both nodes sent heartbeats on 0x100: a simultaneous start collides on the data byte, retries in lockstep | design review | per-node ids 0x101 / 0x102 (`firmware/common/can_msgs.hpp`) |
| No INA219 on this bench: every current read NACKs → `sensor_ok` false → battery latches FAULT, then SAFE, at boot | checking the parts list against the code | I2C and INA219 code removed; current reads 0 mA |
| MCAN RX-FIFO "message lost" flag is sticky: after one overflow every later frame would count as an error | reading `bsp_can.c` against the M_CAN register description | clear `IR.RF0L` when it's seen (write-1-to-clear) |
