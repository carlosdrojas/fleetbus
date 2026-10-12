# Toolchain

Host: macOS (Apple silicon). Install:

```
brew install cmake ninja go stlink
brew install --cask gcc-arm-embedded   # arm-none-eabi-gcc; the installer asks for sudo
```

Use the **cask**, not the `arm-none-eabi-gcc` brew formula: the formula ships no newlib-nano (`--specs=nano.specs` fails), which the firmware needs.

Docker Desktop runs Postgres (`docker compose -f deploy/docker-compose.yml up db`).

## Check
```
cmake --version && ninja --version && go version
arm-none-eabi-gcc --version
st-flash --version
docker compose -f deploy/docker-compose.yml up -d --wait db
```

## Signing keys
The OTA private key lives in `~/.fleetbus/keys/`, never in the repo (`.gitignore` blocks `*.key` and `keys/`). Key generation and handling are documented in M10.

## Notes
- Firmware builds with CMake + arm-none-eabi-gcc; HAL, FreeRTOS, Monocypher and Unity come in as submodules in M2 (see `docs/decisions.md` D2).
- CI workflows in `.github/workflows/` are stubs until the milestone named in each one.
