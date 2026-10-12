#!/usr/bin/env python3
"""Build signed demo images for the simulator: the sim reads byte 0 of the app body as its behaviour.

  v2 good        · v3 hangs at boot (watchdog loop → bootloader rolls back) · v4 runs but never becomes healthy

Signed with the SIM key (FLEETBUS_SIM_KEY hex seed, default = the built-in demo key). These images only run on the
simulator; the real boards use the keys in ~/.fleetbus/keys.
Usage: demo_images.py OUT_DIR [--node body|battery]
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "gateway"))
from fleetbus.image import NODES, build_image  # noqa: E402
from fleetbus.simfleet import sim_key  # noqa: E402

VARIANTS = {"good": (2, 0), "hang": (3, 1), "unhealthy": (4, 2)}

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--node", default="body", choices=("body", "battery"))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    key = sim_key()
    for name, (version, behaviour) in VARIANTS.items():
        body = bytes([behaviour, version]) + (b"D" if a.node == "body" else b"B") * 2000
        for slot, letter in ((0, "A"), (1, "B")):
            blob = build_image(body, NODES[a.node], slot, version, key)
            (out / f"{a.node}_v{version}_{name}_{letter}.signed").write_bytes(blob)
        print(f"{a.node} v{version} ({name}): {out}/{a.node}_v{version}_{name}_[AB].signed")
    print("pubkey (hex):", bytes(key.verify_key).hex())
    return 0

if __name__ == "__main__":
    sys.exit(main())
