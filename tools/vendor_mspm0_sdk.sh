#!/usr/bin/env bash
# Vendor the parts of TI's MSPM0 SDK that FleetBus builds against, pinned to one release tag, into
# firmware/third_party/mspm0_sdk/. Only what an MSPM0G3507 build needs is kept (the full SDK is ~440 MB).
#   DriverLib (source/ti/driverlib, no prebuilt lib/ or SysConfig .meta)      BSD-3-Clause
#   Device headers + GCC startup for MSPM0G350x (source/ti/devices)           BSD-3-Clause
#   CMSIS-Core headers the SDK release was tested with                        Apache-2.0
# Every vendored file carries its own BSD-3-Clause or Apache-2.0 header (checked below); the SDK manifest
# (MANIFEST.html) lists them. SDK_INSTALLER_AGREEMENT.txt is TI's umbrella agreement for the whole SDK download,
# kept for reference. Re-run to update; review the diff.
set -euo pipefail
TAG=${1:-mspm0_sdk_2_11_00_07}
root=$(cd "$(dirname "$0")/.." && pwd)
dst=$root/firmware/third_party/mspm0_sdk
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

git clone -q --depth 1 --branch "$TAG" --filter=blob:none --sparse https://github.com/TexasInstruments/mspm0-sdk.git "$tmp/sdk"
git -C "$tmp/sdk" sparse-checkout set source/ti/driverlib source/ti/devices source/third_party/CMSIS/Core/Include
src=$tmp/sdk/source
rev=$(git -C "$tmp/sdk" rev-parse HEAD)

rm -rf "$dst"
mkdir -p "$dst/ti/driverlib/m0p/sysctl" "$dst/ti/devices/msp/m0p" "$dst/ti/devices/msp/peripherals" "$dst/CMSIS/Core/Include"
cp "$src"/ti/driverlib/*.[ch] "$dst/ti/driverlib/"
cp "$src"/ti/driverlib/m0p/*.[ch] "$dst/ti/driverlib/m0p/"
cp "$src"/ti/driverlib/m0p/sysctl/dl_sysctl_mspm0g1x0x_g3x0x.[ch] "$dst/ti/driverlib/m0p/sysctl/"
cp "$src"/ti/devices/DeviceFamily.h "$dst/ti/devices/"
cp "$src"/ti/devices/msp/msp.h "$dst/ti/devices/msp/"
cp "$src"/ti/devices/msp/m0p/mspm0g350x.h "$dst/ti/devices/msp/m0p/"
cp "$src"/ti/devices/msp/peripherals/*.h "$dst/ti/devices/msp/peripherals/"
cp -R "$src"/ti/devices/msp/peripherals/m0p "$dst/ti/devices/msp/peripherals/" 2>/dev/null || true
cp "$src"/ti/devices/msp/m0p/startup_system_files/gcc/startup_mspm0g350x_gcc.c "$dst/ti/devices/msp/m0p/"
cp "$src"/third_party/CMSIS/Core/Include/*.h "$dst/CMSIS/Core/Include/"
cp "$tmp"/sdk/license_*.txt "$dst/SDK_INSTALLER_AGREEMENT.txt"
missing=$(find "$dst" -name '*.[ch]' -exec grep -L -E 'Redistribution and use|Apache' {} \;)
if [ -n "$missing" ]; then echo "files without a BSD/Apache header:"; echo "$missing"; exit 1; fi
cp "$tmp"/sdk/manifest_*.html "$dst/MANIFEST.html"
cat > "$dst/VERSION" <<EOF
TexasInstruments/mspm0-sdk $TAG ($rev)
Vendored by tools/vendor_mspm0_sdk.sh: DriverLib + MSPM0G350x device headers/startup + CMSIS-Core headers only.
Per-file licenses: BSD-3-Clause (TI DriverLib, device headers, startup) and Apache-2.0 (CMSIS-Core), as stated in
each file header and in MANIFEST.html. Do not edit files here; adapt in firmware/bsp/mspm0g3507 instead.
EOF
du -sh "$dst"
