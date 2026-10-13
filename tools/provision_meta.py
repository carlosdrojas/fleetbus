#!/usr/bin/env python3
"""Write the factory boot-metadata blob for a fresh board (see gateway/fleetbus/bootmeta.py).

  tools/provision_meta.py --board mspm0g3507 --slot 0 --version 1 -o build/meta.bin
prints the flash address to write it at. Flash it together with the bootloader and the first signed image
(tools/flash_mspm0.sh meta ... does all of that). The record says "slot A is NEW"; the bootloader still verifies the
image's signature, hw id and node before booting it, and the app must confirm it within its health window."""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gateway"))
from fleetbus.bootmeta import BOARDS, STATUS, provisioning_blob  # noqa: E402

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--board", choices=BOARDS, required=True)
    ap.add_argument("--slot", type=int, choices=(0, 1), default=0)
    ap.add_argument("--version", type=int, required=True)
    ap.add_argument("--status", choices=[s for s in STATUS if s in ("new", "confirmed")], default="new")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    addr, blob = provisioning_blob(a.board, a.slot, a.version, a.status)
    Path(a.out).write_bytes(blob)
    print(f"0x{addr:08x}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
