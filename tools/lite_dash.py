#!/usr/bin/env python3
"""FleetBus Lite live dashboard. Thin wrapper: the code is gateway/fleetbus/lite_dash.py.

  tools/lite_dash.py --sim                                   no hardware: simulator running the real firmware logic
  tools/lite_dash.py --can slcan:/dev/tty.usbmodem1          the bench bus through the CANable (receive only)
Open http://127.0.0.1:8000."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "gateway"))
from fleetbus.lite_dash import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
