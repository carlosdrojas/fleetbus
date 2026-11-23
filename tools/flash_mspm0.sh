#!/usr/bin/env bash
# Flash an LP-MSPM0G3507 over its on-board XDS110 debugger. Every image is linked at 0x0.
#
#   tools/flash_mspm0.sh bench bringup|can_selftest|io_selftest     bench images (P2, P3, P4)
#   tools/flash_mspm0.sh node  battery|body                         the node firmware
#
# Backend: FLASHER=dslite (default) or FLASHER=openocd.
#   dslite:  TI DSLite from Code Composer Studio (DSLITE, default /Applications/ti/ccs2101/...) with the MSPM0G3507 +
#            XDS110 target file CCS ships (DSLITE_CCXML). Flashes the ELF, so addresses come from the linker.
#            With two LaunchPads plugged in, unplug the one you are not flashing.
#   openocd: needs an OpenOCD with MSPM0 support (built from current source; Homebrew's 0.12.0 has no MSPM0 target).
# Never writes NONMAIN (the boot configuration sector): a bad NONMAIN can lock the device for good (TRM SLAU846).
set -euo pipefail
cd "$(dirname "$0")/.."
FW=build/fw-mspm0/arm/mspm0g3507
FLASHER=${FLASHER:-dslite}
CCS=${CCS:-/Applications/ti/ccs2101/ccs}
DSLITE=${DSLITE:-$CCS/ccs_base/DebugServer/bin/DSLite}
DSLITE_CCXML=${DSLITE_CCXML:-$CCS/scripting/examples/debugger/mspm0g3507/mspm0g3507.ccxml}

flash() {   # flash <name>
  local elf=$FW/$1.elf
  [ -f "$elf" ] || { echo "missing $elf (run: make fw)" >&2; exit 1; }
  case $FLASHER in
    dslite)  [ -x "$DSLITE" ] || { echo "missing $DSLITE (set CCS or DSLITE)" >&2; exit 1; }
             [ -f "$DSLITE_CCXML" ] || { echo "missing $DSLITE_CCXML (set DSLITE_CCXML)" >&2; exit 1; }
             "$DSLITE" flash --config="$DSLITE_CCXML" --flash --verify "$elf" ;;
    openocd) # shellcheck disable=SC2086
             openocd ${OPENOCD_CFG:--f interface/xds110.cfg -f target/ti/mspm0.cfg} -c "program $elf verify reset exit" ;;
    *) echo "FLASHER must be dslite or openocd" >&2; exit 2 ;;
  esac
}

case ${1:-} in
  bench) flash "${2:?bringup|can_selftest|io_selftest}" ;;
  node)  flash "node_${2:?battery|body}" ;;
  *) sed -n '2,12p' "$0"; exit 1 ;;
esac
