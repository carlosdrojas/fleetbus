#!/usr/bin/env python3
"""Generate firmware/common/can_msgs.{h,c} from can/fleetbus.dbc (cantools). Run after editing the DBC."""
import sys
from pathlib import Path

import cantools

ROOT = Path(__file__).resolve().parent.parent

def main() -> int:
    db = cantools.database.load_file(ROOT / "can" / "fleetbus.dbc")
    out = ROOT / "firmware" / "common"
    from cantools.database.can.c_source import generate
    header, source, _, _ = generate(db, "can_msgs", "can_msgs.h", "can_msgs.c", "can_msgs_fuzzer.c")
    (out / "can_msgs.h").write_text(header)
    (out / "can_msgs.c").write_text(source)
    print(f"wrote {out/'can_msgs.h'} and {out/'can_msgs.c'} ({len(db.messages)} messages)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
