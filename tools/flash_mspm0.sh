#!/usr/bin/env bash
# Flash an LP-MSPM0G3507 over its on-board XDS110. UNVERIFIED: no LaunchPad has been flashed with this yet (P0 R6 is
# open). Proving one of these two paths is the first step of P2 on the bench (docs/mspm0-bench-steps.md).
#
#   tools/flash_mspm0.sh bringup                       blink + log image at 0x0 (no bootloader)
#   tools/flash_mspm0.sh boot  battery|body|telematics bootloader at 0x0
#   tools/flash_mspm0.sh app   battery|body|telematics A|B VERSION   sign build/fw-mspm0/.../app_<node>_<slot>.elf.bin
#                                                                    for this board and write it at the slot base
# Backend: FLASHER=openocd (default) or FLASHER=dslite.
#   openocd: needs an OpenOCD with MSPM0 support (built from current source; 0.12.0 from Homebrew may not have it).
#            OPENOCD_CFG defaults to "-f interface/xds110.cfg -f target/ti/mspm0.cfg"; override if your tree differs.
#   dslite:  TI UniFlash command line. Export a target configuration (.ccxml) for MSPM0G3507 + XDS110 from the
#            UniFlash GUI once and point DSLITE_CCXML at it. The dslite flags below are from memory of the UniFlash
#            docs: confirm with `dslite.sh --help` the first time.
# Never writes NONMAIN (the boot configuration sector): a bad NONMAIN can lock the device for good (TRM SLAU846).
# Flash map: firmware/boards/mspm0g3507/fb_board.h (boot 0x0, meta 0x7000/0x7800, slot A 0x8000, slot B 0x14000).
set -euo pipefail
cd "$(dirname "$0")/.."
FW=build/fw-mspm0/arm/mspm0g3507
FLASHER=${FLASHER:-openocd}

write() {   # write <file> <address>
  local f=$1 addr=$2
  [ -f "$f" ] || { echo "missing $f (run: make fw-mspm0)" >&2; exit 1; }
  case $FLASHER in
    openocd) # shellcheck disable=SC2086
             openocd ${OPENOCD_CFG:--f interface/xds110.cfg -f target/ti/mspm0.cfg} -c "program $f $addr verify reset exit" ;;
    dslite)  : "${DSLITE_CCXML:?export DSLITE_CCXML=/path/to/MSPM0G3507_XDS110.ccxml}"
             dslite.sh --config="$DSLITE_CCXML" --flash --verify "$f,$addr" ;;
    *) echo "FLASHER must be openocd or dslite" >&2; exit 2 ;;
  esac
}

case ${1:-} in
  bringup) write "$FW/bringup.elf.bin" 0x00000000 ;;
  boot)    write "$FW/bootloader_${2:?node}.elf.bin" 0x00000000 ;;
  app)
    node=${2:?node} slot=${3:?A or B} ver=${4:?version}
    [ "$slot" = A ] && { addr=0x00008000; idx=0; } || { addr=0x00014000; idx=1; }
    out=build/fw-mspm0/app_${node}_${slot}.v${ver}.signed
    .venv/bin/python tools/sign_image.py sign "$FW/app_${node}_${slot}.elf.bin" --board mspm0g3507 \
      --node "$node" --slot "$idx" --version "$ver" -o "$out"
    write "$out" "$addr" ;;
  *) sed -n '2,17p' "$0"; exit 1 ;;
esac
