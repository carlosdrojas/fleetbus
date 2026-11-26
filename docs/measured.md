# Measured on hardware

Numbers measured on the real boards go here first, one row per measurement, with the date, the board and the method.
Until a row has a value, the matching claim anywhere else stays labelled TARGET, ESTIMATE or "in simulation".
Procedure for each row: `docs/mspm0-bench-steps.md`.

P2 bring-up passed on both boards on 2026-11-24. P3 loopback and two-node CAN traffic at 500 kbit/s ran on 2026-11-25.

| Step | Quantity | Expected (source) | Measured | Date, board, method |
|---|---|---|---|---|
| P2 | Flash with DSLite from the command line | works (unproven) | works, after the 8-byte flash-block linker fix (first try failed verify at 0x1600) | 2026-11-24, A (XDS MG350001) and B (MG35000B), `XDS_SERIAL=… tools/flash_mspm0.sh bench bringup` |
| P2 | Bring-up console, both boards | banner + one line per second | one line per second, `t` steps 1000 ms, `free_heap=9944` steady (A to n=268, B to n=255) | 2026-11-24, A and B, console read at 115200 |
| P2 | LED1 period, bring-up image | 500.0 ms (250 ms toggle) | | |
| P2 | Tick drift over 10 min | within ~1 % (ESTIMATE, SYSOSC spec) | | |
| P3 | CAN loopback self-test | 100/100 | 100/100 PASS, errors=0 | 2026-11-25, A, `can_selftest` console (B's loopback line not captured) |
| P3 | CAN bit time on the bus | 2.000 µs (500 kbit/s, arithmetic) | 2000.1 ns (499.97 kbit/s), least-squares fit over ~70 bits of one frame, 3 frames agree to 0.1 ns, edge residual ≤ 8 ns | 2026-11-25, A's TX (PA12) on AD2 scope ch1 at 25 MS/s; script fit of all edges; capture `docs/captures/2026-11-25-p3-a-tx-frame-25MSps.csv` |
| P3 | Diode-bus rise time, 10–90 % | well under 2 µs (1 kΩ pull-up, short wires) | ~10 ns, not the RC: the 1N4007s conduct backwards while recovering, so the releasing TX pin drives the edge. Then ringing and an overshoot, see below | 2026-11-25, bus on AD2 ch1 and ch2 at 100 MS/s |
| P3 | Bus dominant (low) level | well under 1 V | 0.71–0.73 V (one 1N4007 drop) | 2026-11-25, both scope channels on the bus, 100 MS/s |
| P3 | Bus recessive (idle) level | 3V3 rail | 3.28–3.30 V after edges settle; rail at R1 = 3.277 V | 2026-11-25, AD2 ch2 |
| P3 | Bus overshoot after each release | none expected | **OPEN:** ~3.77 V for 2–8 µs, peaks 4.0–4.3 V, above VDD + 0.3 V. Both channels agree (mean diff 7 mV). Present with only B on the bus. Not A, B's TX (clean 3.29 V while the bus sat at 3.76 V), the AD2 inputs or DIO0. Two parts: (1) ringing peaks, which a 100 Ω in series with A's TX cut from 4.22 V to 3.85 V on A's releases while B's unchanged releases still peaked ~4.1 V, so that part is edge speed plus wire inductance; (2) a ~3.8 V plateau for ~5 µs, then a drop to 3.3 V in ~2 µs, unchanged by the resistor; shape matches 1N4007 storage time, but where the extra 0.5 V comes from is unexplained. Next test if ever: Schottky or 1N4148 diodes | 2026-11-25, AD2 ch1 + ch2 on the same bus point; then ch1 on B's TX, 100 MS/s and 10 MS/s |
| P3 | Two-node traffic, both directions (short run) | each board prints the other's 0x7F0 counter at 10 Hz | works: A and B each printed 122 `rx id=7f0` lines in 12 s, counters consecutive; A `can_err=0` to t=30 s; B `can_err=18`, all from before A was running, flat to t=50 s | 2026-11-25, A and B on the diode bus, 500 kbit/s build, both consoles read at 115200 |
| P3 | Two-node `can_err` after 10 min | 0 | 0 new errors in 620 s (B `can_err` 36358 → 36358), 6296 frames received from A, 0 tx fails | 2026-11-25, B console; A on a USB charger, B on the Mac; diodes 1N4007 (Elegoo Mega 2560 kit), 500 kbit/s |
| P4 | ADC error at 3 points vs multimeter | | board reads low by 17 / 15 / 9 mV at 1591 / 2469 / 3300 mV (and 18 mV at 1109 mV); full travel 0–3291 mV | 2026-11-25, A `io_selftest` ADC ch2 (PA24, pot wiper) vs AD2 scope ch1 averaged 10 ms (no multimeter; the AD2 is not a calibrated reference), 70 s sweep logged in `docs/captures/2026-11-25-p4-adc-sweep-*` |
| P4 | Thermistor input (PA25) | ~1650 mV at room temperature, falls when warmed | 1725 mV at room temperature (AD2 1725–1729), fell to ~1380 mV when held for 20 s, recovered after; board reads 6–19 mV below the AD2 | 2026-11-25, same sweep, ADC ch1 vs AD2 ch2 |
| P4 | Servo pulse widths 0 / 90 / 180° | 1.00 / 1.50 / 2.00 ms at 20 ms | after the bug-6 fix: 0.988 / 1.234 / 1.481 / – / 1.975 ms for 0 / 45 / 90 / 135 / 180°, period 19.768 ms (all 1.2 % short: MCLK fast, see SYSOSC row). Before the fix: 7.90–15.80 ms every 158.1 ms | 2026-11-25, B `io_selftest`, PB4 on AD2 ch1 at 1 MS/s (widths) and 200 kS/s (period) |
| P4 | MCLK error (SYSPLL referenced to SYSOSC) | ±~1 % (internal RC oscillator) | +1.2 % on board B: servo period 158.12 ms vs 160.00 ms (prescale-79 image) and 19.768 ms vs 20.000 ms after the fix → MCLK ≈ 80.95 MHz. CAN is unaffected (crystal): 499.97 kbit/s. FreeRTOS tick, UART and timers inherit it | 2026-11-25, B, AD2 period measurement |
| P4 | Servo period and widths after moving the SYSPLL reference to the 40 MHz crystal | 20.000 ms; 1.000 / 1.500 / 1.750 ms | 20.0004–20.0009 ms; 0.999 / 1.499 / 1.750 ms | 2026-11-25, B `io_selftest`, AD2 ch1 at 200 kS/s |
| P5 | Heartbeat period, 0x101 and 0x102 | 100 ms | | |
| P5 | Two-node bring-up of the node images | body NORMAL ~1 s after hearing the battery | battery NORMAL `peer=1`, body NORMAL `peer=1`, each `rx` +20/s, battery `can_err=0 tx_fail=0`; body alone first: LIMP_HOME, red LED, servo centred (by eye) | 2026-11-25, A `node_battery` and B `node_body` on the Mac, both consoles |
| P5 | Fault FSM on the pot | WARNING ≤ 3.30 V, FAULT ≤ 3.00 V, SAFE after 10 more fault samples, latched | WARNING, then FAULT at the pot's bottom end (`mv` 3000–3001), body LIMP_HOME in the same 1 s print; SAFE ~3 s after FAULT (the pot sits on the 3.000 V threshold, so only some samples count toward `safe_after`, as `battery_fault.cpp` is written); SAFE stayed latched with the pot back at 3597–4187 mV; WARNING cleared on the way up (3392 → NORMAL at 3686) | 2026-11-25, both consoles, timestamped |
| P5 | Battery reset held ~5 s, then released | body LIMP_HOME ~0.5 s after silence; NORMAL ~1 s after the first heartbeat | body `peer=0` LIMP_HOME throughout; battery rebooted to NORMAL (latch cleared); body `peer=1` 0.45 s later but still LIMP_HOME, NORMAL on the next print (≤ 1.45 s after the banner). Exact times: F1/F2 with the analyser | 2026-11-25, both consoles, 1 s print resolution |
| P5 | Battery WARNING / FAULT "pack" voltage on the pot | 3.30 V / 3.00 V (firmware defaults) | | |
| P5 | Node task worst-case execution (`node_us_max`) | a few hundred µs (ESTIMATE) | battery 626 µs, body 175 µs (max since boot, unchanged over the 10-min soak) | 2026-11-25, A `node_battery` + B `node_body`, both consoles for 10.4 min, `docs/captures/2026-11-25-p5-soak10-consoles.log` |
| P5 | Node task worst start lateness (`late_us_max`) | < 1000 µs | battery 3 µs, body 7 µs (body with the display task running) | 2026-11-25, A `node_battery` + B `node_body`, both consoles for 10.4 min, `docs/captures/2026-11-25-p5-soak10-consoles.log` |
| P5 | Minimum free stack per task after 10 min (`stk`) | > ~200 B each | battery node 1104 / log 784 B; body node 1112 / disp 1060 / log 792 B; heap flat (8912 / 7288 B) | 2026-11-25, A `node_battery` + B `node_body`, both consoles for 10.4 min, `docs/captures/2026-11-25-p5-soak10-consoles.log` |
| P5 | Two-node soak, 10 min | no errors | both NORMAL `peer=1` throughout; battery `can_err` 0 → 0, `tx_fail` 0 → 0; body `can_err` flat at 1746 (all from its time alone); battery `rx` +12500 (20/s) | 2026-11-25, A `node_battery` + B `node_body`, both consoles for 10.4 min, `docs/captures/2026-11-25-p5-soak10-consoles.log` |
| L3 | Display frame time (`disp_us`), typical / worst / first full frame | unknown | needle moving: median 11.7 ms, p90 12.2 ms, max sampled 26.4 ms (30 one-per-second samples); still: ~0.24 ms; first full frame 126.9 ms (boot) | 2026-11-25, pot turned continuously for 25 s; B console (`docs/captures/2026-11-25-l3-body-console.log`) + AD2 record 500 kS/s, ch2 bus, ch1 A's TX (body bursts = bus bursts with A's TX high); first frame from the P5 boot |
| L3 | Body `node_us_max` / `late_us_max` with the display vs battery without | same within tick resolution (TARGET) | body (display) 279 µs / 4 µs; battery (no display) 626 µs / 3 µs. Maxima since boot, which includes the F tests | 2026-11-25, pot turned continuously for 25 s; B console (`docs/captures/2026-11-25-l3-body-console.log`) + AD2 record 500 kS/s, ch2 bus, ch1 A's TX (body bursts = bus bursts with A's TX high) |
| L3 | 0x102 heartbeat spacing while the display redraws | 100 ms ± 1 ms (TARGET) | 100.000–100.004 ms over 246 intervals, mean 100.0028 ms, stdev 1.25 µs, while frames took 11–26 ms. 3 intervals short by exactly 16.4 ms excluded: AD2 USB buffer drops (8192 samples at 500 kS/s), not the firmware | 2026-11-25, pot turned continuously for 25 s; B console (`docs/captures/2026-11-25-l3-body-console.log`) + AD2 record 500 kS/s, ch2 bus, ch1 A's TX (body bursts = bus bursts with A's TX high) |
| L3 | Panel tab that gives correct colours and edges | black tab (default) or `FB_LCD_GREENTAB` | | |
| F1 | Battery dies → body limp-home (last 0x101 frame → body PA0) | 500–520 ms (timeout + polling) | 519.6 ms (end of A's last frame → PB13 rise) | 2026-11-25, AD2 record mode 500 kS/s × 20 s: ch1 = A's TX (PA12, its own frames: 0x101 + 0x200 back to back every 100 ms), ch2 = B's J4.35 test point (PB13, mirrors the fault light); A's reset held ~6.5 s by hand |
| F2 | Battery back → body leaves limp-home (first 0x101 → PA0) | 1000–1020 ms (1 s hold) | 1010.3 ms; 1009.7 ms on a second run (start of A's first frame after reset → PB13 fall) | 2026-11-25, AD2 record mode 500 kS/s × 20 s: ch1 = A's TX (PA12, its own frames: 0x101 + 0x200 back to back every 100 ms), ch2 = B's J4.35 test point (PB13, mirrors the fault light); A's reset held ~6.5 s by hand; second run 12 s |
| F3 | FAULT heartbeat on the bus → body acts (message-to-action latency) | < ~10 ms (10 ms poll) | 8.85 ms from the end of the first frame of A's burst, 8.37 ms from the end of the last (one event; frames not decoded at this rate: a 3-frame burst, 210 / 180 / 204 µs, where A's normal burst is 2 frames, consistent with heartbeat + status + the FaultEvent sent at fault onset) | 2026-11-25, AD2 single capture 500 kS/s, trigger on PB13 rise, 15.7 ms pre-trigger; ch1 A's TX; pot turned down by hand |
| F4 | Bus stuck dominant ~1 s: bus-off, then recovery after release | recovery ≈ 2.8 ms + one poll (128 × 11 bits) | **No bus-off.** Short held 6.64 s (bus 0.01 V). Body `can_err` +142 = `tx_fail` +142 (TX FIFO refusals only; the driver counts bus-off separately); body LIMP_HOME `peer=0` during, NORMAL ~1 s after. Traffic resumed 8 µs after release: a 3.97 ms burst (queued frames, two 8-deep TX FIFOs ≈ 16 × 220 µs), normal 100 ms pattern from +58 ms. Consistent with: a stuck-dominant bus never goes idle, so no node starts a frame and TEC never climbs to 256. The expected bus-off case needs a fault that corrupts frames in flight instead | 2026-11-25, jumper bus → GND by hand; AD2 ch2 on the bus 500 kS/s × 15 s; B console |
| F5 | Bus open (one node alone): TEC behaviour | error-passive, not bus-off | error-active → error-passive after 110 ms (tec=128 rec=0), no bus-off seen (short run, not the full F5 procedure) | 2026-11-25, A sending while B ran a broken image, `can_selftest` fault_task line |
| F5 | Body's TX diode (D2) pulled, node images | battery error-passive, not bus-off | Battery: error-passive, retransmits one frame back to back (bus 38 % dominant), no active error flags (longest dominant run 4 bits), a 29-bit recessive gap per attempt (passive flag + delimiter + intermission + 8-bit suspend), no bus-off. Body: still drives its TX into an open diode, reads its own dominant bits back recessive, so it goes **bus-off** repeatedly (`can_err` − `tx_fail` grew ~80/s = driver bus-off recoveries), receives nothing (`rx` frozen), LIMP_HOME `peer=0`. D2 back: body NORMAL `peer=1`, counters flat | 2026-11-25, AD2 ch2 on the bus 500 kS/s × 15 s, B console; D2 out ~70 s |
| F6 | Bus load | 40 frames/s ≈ 0.9 % of 500 kbit/s (arithmetic) | 40 frames/s: per 100 ms, battery 2 frames (108–110, 110–111 bits SOF→ACK) 6.4 ms before body 2 frames (73, 110–111 bits); + 11 tail bits each = 448 bit-times = 896 µs per 100 ms = 0.90 % | 2026-11-25, AD2 on the bus: 10 s record at 500 kS/s for the 100 ms pattern, then 12 triggered 4 MS/s captures for bit counts (500 kS/s samples once per bit and misses single dominant bits, so it can't count frames) |

## Bugs found on hardware

For each: symptom, how it was found, root cause, fix, commit. This becomes the failure story.

1. **Flash verify failed at 0x1600 (2026-11-24, both boards).** DSLite: "Length of block is 4, but it should be divisible
   by 8 since Flash Programmer writes in 64-bits". The `.data` load image was 4 bytes; the MSPM0 flash programs 64-bit
   words. Host tests can't see it. Fix: pad every flash load block to 8 bytes in `common.ld` (commit f39ff57).

2. **HardFault in TI's PLL setup, only in some images (2026-11-25, A).** Symptom: red LED solid, console blank, even
   after a power cycle; bring-up still worked, the CAN self-test did not, and it had worked one build earlier. Found by
   attaching the debugger (CCS scripting): IPSR = 3 (HardFault), stacked PC in `DL_SYSCTL_configSYSPLL` +0x5a, a
   `str` whose offset register held 0x68774665, which is the instruction pair at the next address, not the literal
   `0x1128` it loads two instructions after the function rewrites `CPUSS->CTL` (icache off). Images where that function
   starts on an 8-byte flash line faulted (can_selftest, io_selftest); the others (bring-up, both nodes) did not, and
   the predicted io_selftest failure reproduced at the same offset. Adding diagnostic prints is what moved it onto a
   line. Fix: set `CPUSS->CTL` to the value the function writes before calling it, then restore (`bsp_clock.c`); both
   aligned images then ran. The mechanism inside the CPU subsystem is my reading of the registers, not from TI errata.
3. **CAN init -1: message RAM init never finished (2026-11-24/25, both boards).** Diagnostic print showed
   `PWREN=1` but `MCANSS_CLKEN=0` and every MCAN wrapper and core register reading 0. TI's own loopback example ran on
   the same board, and writing CLKEN=1 from the debugger with our image halted worked at once (PID and CREL read back),
   so the hardware and the 80 MHz MCLK were fine. Our write came ~0.2 µs after `DL_MCAN_enablePower` (16 cycles at
   80 MHz) and was dropped. Fix: write until it reads back (`bsp_can.c`); it takes 42–43 tries on board A.
4. **CAN init -4: `DL_MCAN_setBitTime` rejected the config (2026-11-25).** The unused data-phase segment was set to the
   nominal 33, but DTSEG1 is 5 bits (max 31) and the driver range-checks it even with FD off. Host tests use a fake
   driver, so they never ran the check. Fix: legal data-phase values from TI's example. Then loopback 100/100 PASS.
5. **Board A silent on the bus after moving it to a USB charger (2026-11-25).** Symptom: B received nothing and
   failed every send; A's LED blinked, so A was running. AD2: A's TX pin switched cleanly and its frames decoded, but
   each was cut short by A's own error flag (it drove a bit low, read the bus still high), and the bus never went low
   while A's TX did. Replacing D1 twice and moving it to new holes changed nothing. Root cause: the breadboard's power
   rails are split in the middle; A's GND was on the left half of the − rail and B's on the right. While both boards
   were on the Mac's USB their grounds were joined through the cables, which hid it. Fix: a jumper across the − rail
   gap. B then received A's frames at once and the 10-minute soak had zero errors.
6. **Servo pulses 8x too long (2026-11-25, B).** AD2 on PB4: 7.90 / 9.87 / 11.85 / 13.83 / 15.80 ms pulses every
   158 ms instead of 1.00–2.00 ms every 20 ms; the SG90 barely moved. The comment said "80 MHz / 8 / (79 + 1) = 1 MHz"
   but that is 125 kHz. Host tests never run the timer. Fix: prescale 9 (`bsp_sensors.c`). The leftover 7.90x
   instead of 8.00x was the second finding: MCLK runs ~1.2 % fast because the SYSPLL is referenced to SYSOSC.
7. **A power bank that switches itself off (2026-11-25).** Board A on a power bank went silent twice mid-test:
   B showed `peer=0` and the AD2 saw A's TX pin floating. Power banks cut out at light loads; a LaunchPad draws
   little. Not a firmware bug, but it cost two F1 runs. Bench rule: battery board on the Mac or a wall supply.

## Bugs found before hardware

Found by review or host tests while preparing for the bench. Good context, but not hardware stories.

| Bug | Found by | Fix |
|---|---|---|
| Release builds were `-O3`, not `-Os`: CMake appends its own `-O3 -DNDEBUG` after the toolchain's flags | reading the build line | override `CMAKE_*_FLAGS_RELEASE` (`firmware/CMakeLists.txt`) |
| Both nodes sent heartbeats on 0x100: a simultaneous start collides on the data byte, retries in lockstep | design review | per-node ids 0x101 / 0x102 (`firmware/common/can_msgs.hpp`) |
| No INA219 on this bench: every current read NACKs → `sensor_ok` false → battery latches FAULT, then SAFE, at boot | checking the parts list against the code | I2C and INA219 code removed; current reads 0 mA |
| MCAN RX-FIFO "message lost" flag is sticky: after one overflow every later frame would count as an error | reading `bsp_can.c` against the M_CAN register description | clear `IR.RF0L` when it's seen (write-1-to-clear) |
