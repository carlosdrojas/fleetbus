# Toolchain

Host: macOS (Apple silicon). Install:

```
brew install cmake ninja go stlink   # stlink only for the STM32 Nucleo
brew install --cask gcc-arm-embedded   # arm-none-eabi-gcc; the installer asks for sudo
```

Use the **cask**, not the `arm-none-eabi-gcc` brew formula: the formula ships no newlib-nano (`--specs=nano.specs` fails), which the firmware needs.

Docker (OrbStack or Docker Desktop) runs Postgres 16: `make db`. The container publishes **host port 5433**, not 5432, because a native Postgres often owns 5432 and silently shadows the container (this bit us: database tests were skipping). Set `FLEETBUS_REQUIRE_DB=1` (the Makefile does) so they fail instead of skip.

Python: `make setup` (venv in `.venv`). Go 1.22+. Node 22 for the dashboard. The Nucleo needs `st-flash` (brew `stlink`).

MSPM0G3507 LaunchPad (primary target): no extra compiler. TI's SDK subset is vendored in `firmware/third_party/mspm0_sdk`
(`tools/vendor_mspm0_sdk.sh` re-creates it, pinned to `mspm0_sdk_2_11_00_07`). Flashing over the on-board XDS110 is **not
proven yet** (P0 risk R6): `tools/flash_mspm0.sh` supports an OpenOCD with MSPM0 support (Homebrew's 0.12.0 may not have
it; build current OpenOCD from source) or TI UniFlash's `dslite` command line. Record what works here after the first
bench session.

## Check
```
cmake --version && ninja --version && go version
arm-none-eabi-gcc --version
st-flash --version
docker compose -f deploy/docker-compose.yml up -d --wait db
```

## Signing keys
The OTA private key lives in `~/.fleetbus/keys/`, never in the repo (`.gitignore` blocks `*.key` and `keys/`). `tools/sign_image.py keygen` creates it and rewrites `firmware/keys/fleetbus_pub.h`; `tools/sign_image.py sign --board ...` and `tools/sign_release.sh` sign images. Replace the dev key before anything leaves the bench.

## Notes
- Firmware builds with CMake + arm-none-eabi-gcc (`-DFB_BOARD=stm32g4|mspm0g3507`); STM32 HAL, FreeRTOS, Monocypher and Unity are submodules, the TI SDK subset is vendored (see `docs/decisions.md` D2).
- CI (`.github/workflows/ci.yml`) builds both boards; it has not run on GitHub yet.
- P0 spike tooling: `firmware/spike/p0/` (emulator-based instruction counts need `unicorn==2.1.3`; 2.1.1 crashes on Apple silicon).
