#!/usr/bin/env bash
# P0 spike: link the bootloader's portable core (boot logic, metadata, image verify, Monocypher Ed25519/SHA-512)
# for Cortex-M4 and Cortex-M0+ under the same flags, and print flash use. Usage: firmware/spike/p0/build.sh [outdir]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
fw=$(cd "$here/../.." && pwd)
out=${1:-$fw/../build/spike-p0}
mkdir -p "$out"
tp=$fw/third_party/monocypher/src
src=("$here/spike_size.c" "$fw/common/boot_logic.c" "$fw/common/boot_meta.c" "$fw/common/image.c" "$fw/common/crc32.c"
     "$tp/monocypher.c" "$tp/optional/monocypher-ed25519.c")
inc=(-I"$fw/common" -I"$tp" -I"$tp/optional")

cpu_flags() { case $1 in
  m4) echo "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb" ;;
  m0plus) echo "-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft" ;;
esac; }
# "today" = the flags the STM32 cross build actually uses (CMake Release appends -O3 after -Os, and PIC is on).
opt_flags() { case $1 in today) echo "-Os -O3 -fPIC" ;; Os) echo "-Os" ;; esac; }
printf '%-8s %-6s %8s %6s %6s\n' cpu flags text data bss
for c in m4 m0plus; do
  for o in today Os; do
    elf=$out/core_${c}_${o}.elf
    # shellcheck disable=SC2046
    arm-none-eabi-gcc $(cpu_flags $c) $(opt_flags $o) -std=gnu11 -ffunction-sections -fdata-sections -fno-common -w \
      "${inc[@]}" "${src[@]}" -nostartfiles --specs=nano.specs --specs=nosys.specs \
      -Wl,--gc-sections -Wl,--no-warn-rwx-segments -T"$here/spike.ld" -Wl,-Map="${elf%.elf}.map" -o "$elf"
    read -r t d b _ < <(arm-none-eabi-size "$elf" | tail -1)
    printf '%-8s %-6s %8s %6s %6s\n' "$c" "$o" "$t" "$d" "$b"
  done
done

# Bench images for bench_emu.py: Monocypher only, same CPU/flag matrix.
for c in m4 m0plus; do
  for o in today Os; do
    # shellcheck disable=SC2046
    arm-none-eabi-gcc $(cpu_flags $c) $(opt_flags $o) -std=gnu11 -ffunction-sections -fdata-sections -fno-common -w \
      "${inc[@]}" "$here/spike_bench.c" "$tp/monocypher.c" "$tp/optional/monocypher-ed25519.c" -nostartfiles \
      --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections -Wl,--no-warn-rwx-segments -T"$here/spike.ld" \
      -fstack-usage -o "$out/bench_${c}_${o}.elf"
  done
done
echo "bench ELFs in $out"
