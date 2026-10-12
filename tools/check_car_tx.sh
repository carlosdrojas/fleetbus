#!/usr/bin/env bash
# Fails if anything in the firmware other than the CAN driver and car_tx.c can transmit on the CAN bus. Run in CI.
# The in-car node must never write to the Sentra: its single TX path is car_tx(), which filters through the OBD allowlist.
set -euo pipefail
cd "$(dirname "$0")/.."
bad=0
hits=$(grep -rn "bsp_can_send(" firmware/app/telematics firmware/common 2>/dev/null | grep -v "car_tx.c" || true)
if [ -n "$hits" ]; then echo "telematics/common code calls bsp_can_send() outside car_tx.c:"; echo "$hits"; bad=1; fi
hits=$(grep -rn "HAL_FDCAN_AddMessageToTxFifoQ" firmware --include=*.c | grep -v "third_party" | grep -v "bsp/bsp_can.c" || true)
if [ -n "$hits" ]; then echo "raw FDCAN transmit outside bsp_can.c:"; echo "$hits"; bad=1; fi
[ $bad = 0 ] && echo "ok: the only car-bus transmit path in the telematics firmware is car_tx() (allowlist-gated)"
exit $bad
