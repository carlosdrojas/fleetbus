# Hardware

Bench parts for the primary target (LP-MSPM0G3507 LaunchPad). Wiring per node and the checks for each stage are in
`docs/mspm0-bench-steps.md`; pins in `firmware/bsp/mspm0g3507/bsp_pins.h` (unverified until bring-up).

| Part | Qty | Why |
|---|---|---|
| TI LP-MSPM0G3507 LaunchPad | 2 (3 with the in-car node) | battery and body nodes; on-board XDS110 debugger + UART backchannel |
| 3.3 V CAN transceiver breakout (SN65HVD230 or TCAN1042) | 1 per node | the LaunchPad has no transceiver (MCAN0 on PA12/PA13) |
| 120 Ω resistors | 2 | termination at the two ends of the bus only |
| Twisted pair + jumpers | | CANH/CANL |
| USB-CAN adapter (CANable or similar, slcan/gs_usb) | 1 | gateway to the laptop, and the HIL tool |
| INA219 breakout | 1 | battery node current (I2C1, PB2/PB3) |
| 10 kΩ NTC + 10 kΩ resistor, 10 kΩ pot | 1 each | battery node temperature and "pack voltage" (ADC0 ch2/ch3) |
| Hobby servo (5 V supply) | 1 | body node actuator (PB4) |
| Adafruit 1.8" TFT breakout (product 358, ST7735R 160×128) | 1 | FleetBus Lite instrument cluster on the body node (`docs/lite.md` L3) |
| USB relay or smart plug | 1 | P6 power-cut test (`tools/powercut.py`) |

Test equipment: multimeter; oscilloscope or logic analyzer (a USB logic analyzer covers LED, servo and bit timing;
CANH/CANL waveforms and the P8 fault captures need a scope).

The STM32 Nucleo-G431RB build is kept as a secondary target with the same roles (pins in `firmware/bsp/stm32g4/bsp_pins.h`).

## The car (2025 Nissan Sentra SR)

Rules from AGENTS.md apply to every item: read-only (standard OBD-II requests only), passenger operates, nothing on the
windshield, unplug when parked. Procedure: `docs/bench-guide.md` sections 6 and 7.

**Day-one listen test** (do this first, before buying the in-car parts):

| Part | Qty | Why |
|---|---|---|
| USB-CAN adapter (the bench one) | 1 | must support a true listen-only mode (`tools/obd_listen.py` refuses otherwise) |
| OBD-II male breakout / pigtail cable (or OBD-II to DB9 for the adapter) | 1 | CAN-H pin 6, CAN-L pin 14, ground pins 4/5 |
| Multimeter | 1 | battery voltage at OBD pin 16 (to ground), engine off and running: sets the engine-on threshold |
| Laptop | 1 | runs the listener and records the trace |

**In-car node (phase 4b, optional):**

| Part | Qty | Why |
|---|---|---|
| LP-MSPM0G3507 LaunchPad | 1 (the third) | telematics node |
| 3.3 V CAN transceiver **with a silent-mode pin** (for example TJA1051T/3 or TCAN1051V; check the datasheet) | 1 | lets the node be forced listen-only in hardware while it is idle |
| OBD-II male breakout with leads | 1 | CAN-H/CAN-L/ground only; **no 120 Ω resistor** (the car's bus is already terminated; adding one changes it) |
| Raspberry Pi (4, 5 or Zero 2 W) + microSD | 1 | runs `gateway --serial`; the LaunchPad's USB (XDS110 backchannel) is the serial link and its power |
| 12 V car USB charger rated for the Pi (5 V 3 A) + cables | 1 | power from the 12 V accessory socket, **not** OBD pin 16 (always on, drains the battery); check whether the Sentra's socket switches off with the ignition |
| Phone hotspot | | uplink to the backend |
| Small enclosure, strain relief, velcro (floor or under a seat, never the windshield) | 1 | keeps it from moving and from pulling on the OBD port |

## Phase 5: custom node board (optional, not designed)

A BoosterPack that plugs onto the LaunchPad and replaces the breadboard. Two variants from one schematic:

- **Bench node:** CAN transceiver, 120 Ω termination on a jumper, INA219 + shunt, thermistor and pot inputs,
  servo header, screw terminals for CANH/CANL, and test points on CAN, the 3.3 V rail and the ADC inputs.
- **In-car node:** the same plus a protected 12 V input (fuse, reverse-polarity protection, TVS diode for load dump),
  a buck converter to 5 V and 3.3 V, an ignition-sense input so the node can power itself down when the car is off
  (fixing today's "unplug when parked" limitation), the silent-mode line to the transceiver, and an OBD-II connector.

Why: it turns a breadboard into a board that survives a car, and it is the electrical-engineering part of the project
(power entry, protection, CAN signal integrity, layout). The deliverable is KiCad files plus a bring-up log with scope
captures (CAN waveforms, rail ripple, ADC noise) compared with what the schematic predicts. Ties to the ECE 302 buck
converter work. Relevant to SpaceX Engineering and Tesla EE roles; not needed for the software roles.
