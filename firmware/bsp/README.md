# BSP

`bsp.h` is the board interface (C). The implementation for the LP-MSPM0G3507 LaunchPad is in `mspm0g3507/`, on TI's
MSPM0 SDK DriverLib (vendored in `third_party/mspm0_sdk`). The pin map is `mspm0g3507/bsp_pins.h`; it comes from TI's
examples and the datasheet and is not verified on a board yet. `bsp_log.c` is a tiny polled printf (no newlib printf).
