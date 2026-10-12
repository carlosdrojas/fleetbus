#!/usr/bin/env bash
# Flash a Nucleo-G431RB through its on-board ST-LINK using st-flash (brew install stlink).
#   tools/flash.sh boot  battery|body                  bootloader (0x08000000), from build/fw
#   tools/flash.sh app   battery|body  A|B  VERSION    sign build/fw/arm/app_<node>_<slot>.elf.bin and write it at the slot base
#   tools/flash.sh erase                               full chip erase (does NOT touch the host: only the target board)
# Flash map (firmware/common/flash_if.h): boot 0x08000000 · meta 0x08007000 · slot A 0x08008000 · slot B 0x08014000
set -euo pipefail
cd "$(dirname "$0")/.."
FW=build/fw/arm
case "${1:-}" in
  boot)  st-flash --reset write "$FW/bootloader_${2:?node}.elf.bin" 0x08000000 ;;
  app)
    node="${2:?battery|body}"; slot="${3:?A|B}"; ver="${4:?version}"
    [ "$slot" = A ] && addr=0x08008000 && idx=0 || { addr=0x08014000; idx=1; }
    out="build/${node}_${slot}.v${ver}.signed"
    .venv/bin/python tools/sign_image.py sign "$FW/app_${node}_${slot}.elf.bin" --node "$node" --slot "$idx" --version "$ver" -o "$out"
    st-flash --reset write "$out" "$addr" ;;
  erase) st-flash erase ;;
  *) sed -n '2,8p' "$0"; exit 2 ;;
esac
