# BSP

`bsp.h` is the board interface every app and the bootloader use. One implementation per board, picked by `FB_BOARD`:

| Folder | Board | Vendor layer | Status |
|---|---|---|---|
| `stm32g4/` | Nucleo-G431RB (Cortex-M4F) | STM32 HAL (submodule) | Compiles and links; never run on a board |
| `mspm0g3507/` | LP-MSPM0G3507 LaunchPad (Cortex-M0+) | TI MSPM0 SDK DriverLib (vendored, `third_party/mspm0_sdk`) | Primary target; see `docs/mspm0-port-plan.md` |

Each board folder has a `bsp_pins.h` with its pin map. Pin maps come from datasheets and user guides and are not verified on hardware.
