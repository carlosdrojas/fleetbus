#!/usr/bin/env bash
# Sign every node app's slot A/B pair for one board and version, ready for `fbctl image upload` (P7 OTA on hardware).
#   tools/sign_release.sh mspm0g3507 2              -> build/release/mspm0g3507/v2/app_<node>_{A,B}.signed
#   tools/sign_release.sh mspm0g3507 3 _unhealthy   -> bench fault variant (needs -DFB_BENCH_FAULT_IMAGES=ON); BENCH ONLY
# Build first: make fw-mspm0 (or make fw for stm32g4). The firmware version comes from --version here; the same number
# must not be reused (the API rejects a duplicate version) and must be higher than the confirmed one (anti-downgrade).
# Then:  build/bin/fbctl image upload -a <A.signed> -b <B.signed> -notes "..."  and  fbctl rollout start -image ID
set -euo pipefail
cd "$(dirname "$0")/.."
board=${1:?board: stm32g4 or mspm0g3507} ver=${2:?version} variant=${3:-}
case $board in
  stm32g4) fw=build/fw/arm/stm32g4; nodes="battery body telematics" ;;
  mspm0g3507) fw=build/fw-mspm0/arm/mspm0g3507; nodes="battery body telematics" ;;
  *) echo "unknown board $board" >&2; exit 2 ;;
esac
out=build/release/$board/v$ver
mkdir -p "$out"
for node in $nodes; do
  for slot in A B; do
    bin=$fw/app_${node}_${slot}${variant}.elf.bin
    [ -f "$bin" ] || { [ -n "$variant" ] && [ "$node" = telematics ] && continue; echo "missing $bin" >&2; exit 1; }
    idx=0; [ "$slot" = B ] && idx=1
    .venv/bin/python tools/sign_image.py sign "$bin" --board "$board" --node "$node" --slot "$idx" --version "$ver" \
      -o "$out/app_${node}_${slot}${variant}.signed" >/dev/null
  done
done
ls -1 "$out"
