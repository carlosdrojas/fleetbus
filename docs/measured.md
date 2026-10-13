# Measured on hardware

Numbers measured on real boards go here, one row per measurement, with the date, the board and the method. Until a row
has a value, the matching claim elsewhere stays labelled TARGET, ESTIMATED or UNVERIFIED. Procedure for each row:
`docs/mspm0-bench-steps.md` (LP-MSPM0G3507, primary) and `docs/bench-guide.md` (car day-one test, STM32 notes).

**Nothing has been measured on hardware yet.**

| Milestone | Quantity | Expected (source) | Measured | Date, board, method |
|---|---|---|---|---|
| P2 | Flash path that works headless (R6) | OpenOCD or UniFlash `dslite` (unproven) | | |
| P2 | LED1 period | 500.0 ms (bring-up image) | | |
| P2 | Tick drift over 10 min | within ~1 % (EST, SYSOSC) | | |
| P2 | GDB attach from the CLI (R6) | works | | |
| P3 | CAN loopback self-test | 100/100 | | |
| P3 | CAN bit time | 2.000 µs (500 kbit/s, arithmetic) | | |
| P3 | Two-node error count after 10 min | 0 | | |
| P4 | ADC error at 3 points vs multimeter | | | |
| P4 | INA219 vs multimeter at a known load | | | |
| P4 | Servo pulse widths (0 / 90 / 180°) | 1.00 / 1.50 / 2.00 ms, 20 ms period | | |
| P5 | Heartbeat period on the bus | per `can/fleetbus.dbc` | | |
| P5 | Node task stack free (minimum) | > 512 B of 4 KB | | |
| P6 | Bootloader reset-to-app time | < 1 s (EST: 0.43–0.65 s per verify at 80 MHz, P0) | | |
| P6 | Power-cut torture test | 200/200 boots | | |
| P7 | OTA transfer time (~29 KB image, 500 kbit/s) | ~5 s (EST from simulation of a 38 KB image ≈ 6 s) | | |
| P7 | Reboot to confirm | ≈ 10 s health window | | |
| P7 | Rollback time, never-healthy image | ≈ 3 × (boot + 10 s deadline + 2 s watchdog) (EST) | | |
| P7 | Rollback time, hanging image | ≈ 3 × (boot + 2 s watchdog) (EST) | | |
| P8 | Bus-off recovery time (CAN_H–CAN_L short removed) | 128 × 11 recessive bits ≈ 2.8 ms at 500 kbit/s + driver poll | | |
| Car | Sentra passive listen: any frames? | unknown (day-one test) | | |
| Car | Engine-off voltage threshold | 13.0 V (TARGET) | | |
