# Bench steps

One milestone at a time, in order. Each step says what to run, what you should see, and what to write in
`docs/measured.md`. Expected values come from TI documents or arithmetic and say so; nothing here has been done yet.
Any time something behaves differently from "expect", stop and write it down (symptom first): that's the failure story.

Common setup, once:

```bash
cd ~/Documents/Dev/fleetbus
make fw                       # images land in build/fw-mspm0/arm/mspm0g3507/
ls /dev/cu.usbmodem*          # each LaunchPad shows two ports; the console is usually the one ending in 1
```

Console: `screen /dev/cu.usbmodemXXXX1 115200` (quit: Ctrl-A then K). If it prints nothing, try the other port. The
J101 jumpers on the LaunchPad must be fitted (they connect the XDS110 to UART0 on PA10/PA11). Press the board's reset
button (NRST) after opening the console to see the banner.

Flashing: `tools/flash_mspm0.sh ...` uses DSLite from Code Composer Studio. With two LaunchPads on USB, unplug the one
you're not flashing (DSLite picks the first XDS110 it finds).

Analog Discovery (WaveForms): Logic tool for digital signals and the CAN decoder, Scope for analog. Always join the AD's
ground to the LaunchPad's GND.

---

## P2: bring-up, one board at a time

Proves the clock tree, GPIO, UART and FreeRTOS. Nothing wired except USB.

```bash
tools/flash_mspm0.sh bench bringup
```

1. **Flash.** Copy the whole output, success or failure.
2. **Console.** Expect `FleetBus bring-up on mspm0g3507`, then once a second
   `bringup mspm0g3507 t=<ms> ms n=<count> free_heap=<bytes>`.
   - Nothing at all and the red LED (LED1) **solid on**: stuck in clock setup (`bsp_clock_init`: the PLL lock check or
     the 40 MHz crystal). Tell me; don't change anything.
   - Garbage characters: the UART baud clock assumption (ULPCLK = 40 MHz, `bsp_core.c`) is wrong.
   - Nothing and the LED off: try the other serial port, check J101.
3. **LED period.** LED1 toggles every 250 ms. Logic analyser DIO0 on **PA0**, GND to GND: measure the period.
   Expect 500.0 ms. About 625 ms or 1250 ms would mean the CPU isn't on the 80 MHz PLL.
4. **Tick accuracy.** Leave it 10 minutes; compare the `t=` value with your phone's stopwatch. The tick comes from the
   PLL fed by SYSOSC; expect within about 1 % (ESTIMATE from the datasheet's SYSOSC accuracy).

Repeat on the second board. **Write down:** flasher output (worked or not), LED period, 10-minute drift, `free_heap`.

---

## P3: CAN between the two boards (diode bus)

No transceivers, so the bench bus is a wired-AND on 3.3 V logic. Bench only: it's not a real CAN physical layer
(no differential pair, no termination), but the CAN controller can't tell the difference, so the firmware is the same.

**Wiring** (both boards unpowered):

```
           3V3 ──[ 1 kΩ ]──┬───────────────── bus line ──────────────────┬──── AD DIO0 (CAN decoder)
                           │                                             │
  board A:  PA12 (TX) ──|◄─┤  Schottky, band (cathode) toward PA12       ├─►|── PA12 (TX)  board B
            PA13 (RX) ─────┘  RX straight to the bus line                └───── PA13 (RX)
            GND ───────────────────────── GND ─────────────────────────────────── GND ── AD GND
```

Parts: two Schottky diodes (BAT54, BAT43, 1N5817; a 1N4148 works but the low level is higher), one 1 kΩ resistor.
**As built (2026-11-25):** 1N4007s at 500 kbit/s (low level 0.71 V, 10-minute soak with 0 errors), plus a 1 kΩ in
series with each RX pin: with 1N4007s the bus briefly overshoots to ~3.8–4.3 V after each release (docs/measured.md),
and the resistor keeps that current out of the RX pins. Breadboard power rails are often **split in the middle**:
join both halves of the − rail, or a board on a separate supply loses its ground (bug 5). Run the battery board from
the Mac or a wall supply, not a power bank: power banks switch off at light loads (bug 7).
Only 1N4007 rectifiers on hand (starter kits ship these): try them at 500 kbit/s first. They switch slowly and drop
about 0.7 V, so check the scope for a low level well under 1 V and watch `can_err`. If errors appear, rebuild at a
lower rate with `make fw CAN_KBPS=125` (or 250), flash both boards, and set the WaveForms decoder to the same rate.
Record which diodes and which rate you ran in `measured.md`.
Take the pull-up's 3V3 from the **body board** (B, J1.1) and never join the two boards' 3V3 pins. The fault tests silence the
battery board by holding its reset; if you unplug it instead, its unpowered TX pin sits near 0 V and pulls the bus low
through its diode, jamming the bus for the body too.
Keep wires short (under ~30 cm). Before powering: check PA12 and PA13 on the LaunchPad silkscreen. The pin map
(`firmware/bsp/mspm0g3507/bsp_pins.h`) is from TI's examples and not verified yet.

How it works: a TX pin driving low (dominant) pulls the bus low through its diode; driving high (recessive), its diode
blocks and the 1 kΩ pulls the bus high. Every RX reads the bus. Rise time with 1 kΩ and short wires is well under a
bit time (2 µs).

```bash
tools/flash_mspm0.sh bench can_selftest      # on both boards, one at a time
```

1. **Loopback** (first line after the banner, per board): `CAN loopback: 100/100 PASS`. This runs inside the controller;
   a FAIL is the driver or the clock, not the wiring. The banner line must say `init 0`.
2. **Two nodes.** Both boards running and wired: each console prints `rx id=7f0 dlc=4 n=<the other board's counter>`
   ten times a second, and the `t=... sent=... can_err=...` line every 5 s must show `can_err` not climbing.
3. **Bit timing.** WaveForms Logic: DIO0 on the bus line, add a CAN decoder at 500 kbit/s, trigger on falling edge.
   Measure one bit (the narrowest pulse): expect 2.000 µs. Sample point: 87.5 % by configuration (`bsp_can.c`); you
   can't see that on the wire, but a bit time far from 2 µs means the CAN clock isn't the 40 MHz crystal.
   Save a screenshot of one decoded 0x7F0 frame (id, DLC, data, ACK slot).
4. **Bus check.** Zoom on one edge: the rise (low to high) is the RC of the 1 kΩ and the wiring. Note its 10–90 % time.

**Write down:** loopback result, bit time, rise time, `can_err` after 10 minutes, screenshot file name.

---

## P4: sensors and actuators (io_selftest)

Wire what you have (any subset is fine):

| What | Wire | Note |
|---|---|---|
| Pot (pack voltage) | ends to 3V3 and GND, wiper to **PA24** (ADC ch2 in the log) | 3.0–4.2 V "pack" across its travel |
| Temperature | 10 kΩ from 3V3 to **PA25**, the kit's thermistor from PA25 to GND (ADC ch1) | Not the DHT11 (that's a digital sensor). Never leave PA25 floating: it can read as overtemperature and trip a fault. |
| Servo | signal **PB4**, power 5 V, GND joined | a micro servo can run from the LaunchPad's 5V pin for light loads; add ~100 µF across its supply |
| Lights | on-board LED2 (PB27 green = head, PB22 blue = turn), LED1 red (PA0) = fault | nothing to wire |
| Current | no sensor | the firmware reports 0 mA |

```bash
tools/flash_mspm0.sh bench io_selftest
```

Every 500 ms: `adc ch1=<mV> mV ch2=<mV> mV servo=<deg> deg lights=<mask>`.

1. **ADC.** Turn the pot end to end: ch2 about 0 → 3300 mV. Measure PA24 with a multimeter at three points; note the error.
2. **Temperature input.** ch1 about 1650 mV at room temperature (thermistor ≈ 10 kΩ at 25 °C). Pinch the
   thermistor: ch1 should **fall** within seconds (warmer = lower resistance = lower voltage). If it rises, the
   thermistor and the 10 kΩ are swapped: the lookup curve in `firmware/app/ntc_lut.inc` assumes the thermistor on the
   GND side.
3. **Servo.** Logic analyser on PB4: period 20.0 ms, high time 1.00 / 1.25 / 1.50 / 1.75 / 2.00 ms as it steps.
4. **Lights.** LED2 cycles off, green, blue, (LED1 red), green+blue.

**Write down:** ADC error at three points, servo pulse widths, anything mapped differently from the table.

---

## P5: the node firmware on both boards

```bash
tools/flash_mspm0.sh node battery     # board A (with the pot and the temperature divider)
tools/flash_mspm0.sh node body        # board B (servo; display comes in L3)
```

Banner: `FleetBus battery node, fw v1, C++17` (or `body`). Then once a second:

```
t=12000 NORMAL peer=1 rx=… can_err=0 tx_drop=0 tx_fail=0 node_us_max=… late_us_max=… mv=3800 ma=0 dc=250 ok=1 stk=…, heap=…
```

| Field | Meaning | Expect |
|---|---|---|
| state | this node's state | battery NORMAL; body NORMAL once it has heard the battery for 1 s, LIMP_HOME before |
| `peer` | other node's heartbeat seen within 500 ms | 1 |
| `can_err` | driver errors (TX FIFO full, RX overflow, bus-off) | 0, steady |
| `tx_fail` | frames the CAN TX FIFO refused | 0 with both boards up; **climbs if a board is alone** (nobody ACKs) |
| `node_us_max` | worst node-task execution time since boot | a few hundred µs (ESTIMATE) |
| `late_us_max` | worst start lateness of the 10 ms node task | under 1000 µs (tick resolution) |
| `stk` | free stack per task in bytes (battery: node, log; body: node, disp, log) | none under ~200 |

1. **Alone.** Battery alone: NORMAL, `peer=0`, `tx_fail` climbing (expected). Body alone: LIMP_HOME, red LED on, servo
   centred. Then wire both: within ~1 s the body goes NORMAL, LED2 green (head light).
2. **Bus.** WaveForms CAN decoder on the bus: 0x101 and 0x102 every 100 ms, 0x200 and 0x201 every 100 ms. Decode
   offline: export the decoder's CSV and compare the bytes with the table in `firmware/common/can_msgs.hpp`.
3. **Warning and fault.** Turn the pot slowly down: battery WARNING below 3.30 V "pack" (`mv=` on its console), FAULT
   below 3.00 V, then SAFE about 1 s later. Body goes LIMP_HOME (red LED, servo to 90°) as soon as it sees FAULT.
   Turning the pot back up does **not** clear FAULT/SAFE: they latch until the battery resets (by design).
4. **Silence.** Hold the battery's reset button: body LIMP_HOME after about half a second. Release: body back to
   NORMAL about 1 s after the battery's first heartbeat.
5. **Watchdog.** Not testable by halting in the debugger (WWDT pauses while the CPU is halted). Leave for later.

**Write down:** heartbeat period on the analyser, the `mv=` values where WARNING and FAULT happened, `stk` minimums
after 10 minutes, `node_us_max`, `late_us_max`.

---

## L3: the display on the body node

Adafruit 1.8" TFT (product 358, ST7735R), body node unpowered:

| Breakout | LaunchPad |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCK | PB9 |
| MOSI (SI) | PB8 |
| TFT_CS | PB6 |
| D/C | PB16 |
| RESET | PB15 |
| LITE | 3V3 (backlight full on) |
| MISO, CARD_CS | not connected |

Reflash isn't needed (the body image always drives the display). Power up: the cluster shows the title bar, the
voltage gauge, TEMP / CURR / SOC, and a banner (NORMAL green, LIMP HOME amber, BATTERY SILENT red...).

1. Wrong colours or a 1–2 px garbage edge: it's a green-tab panel. `cmake -S firmware -B build/fw-mspm0 -DFB_LCD_GREENTAB=ON && make fw`, reflash body.
2. **Draw time.** The body console adds `disp_us=` (last frame). Turn the pot: frames with a moving needle cost more.
   Note the largest you see, and the first frame's (full screen) if you catch it.
3. **CAN task untouched.** Compare the body's `node_us_max` and `late_us_max` with the display running against the
   battery's (no display). And on the analyser: spacing of 0x102 heartbeats while you turn the pot: expect 100 ms ± 1 ms.

**Write down:** `disp_us` typical and worst, `node_us_max` / `late_us_max` on both boards, heartbeat spacing, panel tab.

---

## F: fault injection (bench only)

Both node images running, analyser on the bus.

1. **Battery dies.** Hold the battery's reset (as P5.4) and measure on the analyser: the bus (or the battery's TX), and the
   body's **J4.35 test point (PB13)**, which goes high with the fault light (PA0 itself is only on the unpopulated edge
   connector). Time from the end of the last 0x101 frame to the PB13 edge. Measured 519.6 ms. Expect about 500–520 ms: the 500 ms timeout, plus up to 10 ms before the body polls the last
   frame out of its RX FIFO, plus up to one 10 ms tick before it notices the timeout.
2. **Battery recovers.** Release reset: time from the first 0x101 frame to PB13 falling (measured 1010.3 ms). Expect about 1000–1020 ms: the
   1 s recovery hold plus up to two 10 ms ticks (the battery's boot time is before its first frame, so it isn't in this).
3. **Fault to action.** Turn the pot to FAULT: time from the first 0x101 frame whose State is 3 (byte 6, low nibble) to
   PB13 rising. That's the message-to-action latency (measured ~8.4–8.9 ms). Expect under ~10 ms (the body polls every 10 ms).
4. **Bus stuck dominant.** Jumper the bus line to GND for about 1 s, then remove. **Measured: no bus-off.** A bus that
   never goes idle lets no node start a frame, so TEC doesn't climb; `can_err` rises only by TX-FIFO refusals. Traffic
   resumed 8 µs after release with the queued frames, then the normal pattern; body back to NORMAL after its 1 s hold.
   For exact state transitions flash `can_selftest` on both boards instead: it prints
   `can error-active -> error-passive -> BUS-OFF ... after N ms (tec=.. rec=..)` and the recovery. Bus-off recovery
   needs 128 × 11 recessive bits ≈ 2.8 ms at 500 kbit/s (arithmetic) plus up to one poll.
5. **Bus open.** Pull the body's TX diode: the battery is alone (`tx_fail` climbs, TEC rises to error-passive, not
   bus-off: an unacknowledged transmitter stops counting at 128). Put it back: both recover. Measured: the battery goes error-passive and retransmits forever, as predicted; the body
   (whose TX now reads back recessive) goes bus-off over and over, stops receiving, and enters limp-home.
6. **Bus load.** WaveForms: count frames over 10 s. Expect 40 frames/s (two heartbeats + two status messages at 10 Hz);
   at ~115 bits per frame that's ≈ 0.9 % of 500 kbit/s (arithmetic).

**Write down** every number with the setup, and a screenshot per test.
