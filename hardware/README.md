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
| USB relay or smart plug | 1 | P6 power-cut test (`tools/powercut.py`) |

The STM32 Nucleo-G431RB build is kept as a secondary target with the same roles (pins in `firmware/bsp/stm32g4/bsp_pins.h`).

Optional phase 5, a custom node board (transceiver with switchable termination, INA219, thermistor, protected 12 V input
and buck): not designed.
