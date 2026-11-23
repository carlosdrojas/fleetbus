# Toolchain

Host: macOS (Apple silicon).

```bash
brew install cmake ninja
brew install --cask gcc-arm-embedded   # arm-none-eabi-gcc/g++ with newlib-nano; the installer asks for sudo
make setup                             # Python venv for the dashboard and the DBC code generator
```

Use the **cask**, not the `arm-none-eabi-gcc` brew formula: the formula ships no newlib-nano (`--specs=nano.specs`
fails) and no C++ library.

**Flashing:** `tools/flash_mspm0.sh` uses TI's `DSLite` from Code Composer Studio (default path
`/Applications/ti/ccs2101/ccs/ccs_base/DebugServer/bin/DSLite`, override with `CCS=` or `DSLITE=`) and the MSPM0G3507
target file CCS ships. Homebrew's OpenOCD 0.12.0 has no MSPM0 target. Record here what worked after the first flash.

**TI SDK:** the subset the firmware needs is vendored in `firmware/third_party/mspm0_sdk`; `tools/vendor_mspm0_sdk.sh`
re-creates it, pinned to `mspm0_sdk_2_11_00_07`. FreeRTOS and Unity are git submodules.

## Check

```bash
cmake --version && ninja --version && arm-none-eabi-g++ --version
make host-test py-test fw
```

CI (`.github/workflows/ci.yml`) runs the same targets on Ubuntu; it has not run on GitHub yet.
