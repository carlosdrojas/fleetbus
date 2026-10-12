#!/usr/bin/env python3
"""Day-one Sentra test: LISTEN to the car's CAN bus and log every frame. This program has no transmit path.

  tools/obd_listen.py slcan:/dev/tty.usbmodem1101 --seconds 60 --out hil/traces/sentra_idle.log
  tools/obd_listen.py gs_usb:0 --bitrate 500000

Safety (AGENTS.md): never write to the car. This script
  1. opens the adapter in listen-only mode where the driver supports it and REFUSES to continue if it cannot confirm that
     (use --allow-software-only to accept a software-only guarantee, e.g. an slcan adapter on firmware without silent mode,
     and then verify on a scope/analyzer that the adapter never drives the bus),
  2. never calls send(); the Bus object is wrapped so any send raises.
OBD-II mode 01/09 polling lives in the telematics firmware behind a compile-time allowlist, not here.
The OBD-II port is powered with the car off: unplug the adapter when parked.
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "gateway"))

class ListenOnlyBus:
    def __init__(self, interface: str, channel: str, bitrate: int, allow_software_only: bool):
        import can
        self.hw = False
        try:
            self._bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate, state=can.BusState.PASSIVE)
            self.hw = True
        except (TypeError, NotImplementedError, ValueError):
            if not allow_software_only:
                raise SystemExit("adapter/driver does not confirm listen-only mode; refusing to attach to a car. "
                                 "Re-run with --allow-software-only after checking the adapter on a bench bus.")
            self._bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate)
    def recv(self, timeout):
        return self._bus.recv(timeout=timeout)
    def send(self, *a, **k):
        raise PermissionError("listen-only: transmit is disabled")
    def shutdown(self):
        self._bus.shutdown()

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("adapter", help="interface:channel for python-can, e.g. slcan:/dev/tty.usbmodem1101")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--out", default="obd_listen.log")
    ap.add_argument("--allow-software-only", action="store_true")
    a = ap.parse_args()
    iface, channel = a.adapter.split(":", 1)
    bus = ListenOnlyBus(iface, channel, a.bitrate, a.allow_software_only)
    print(f"listening ({'hardware listen-only' if bus.hw else 'SOFTWARE-ONLY guarantee'}) for {a.seconds:.0f}s; frames -> {a.out}")
    ids: dict[int, int] = {}
    t0 = time.time()
    with open(a.out, "w") as f:
        while time.time() - t0 < a.seconds:
            m = bus.recv(0.2)
            if m is None or m.is_error_frame:
                continue
            f.write(f"({m.timestamp or time.time():.6f}) can0 {m.arbitration_id:03X}#{bytes(m.data).hex().upper()}\n")
            ids[m.arbitration_id] = ids.get(m.arbitration_id, 0) + 1
    bus.shutdown()
    print(f"{sum(ids.values())} frames, {len(ids)} distinct ids")
    for cid, n in sorted(ids.items(), key=lambda x: -x[1])[:20]:
        print(f"  0x{cid:03X}: {n:6d} frames ({n / a.seconds:.1f}/s)")
    if not ids:
        print("no frames: check wiring (CAN-H pin 6, CAN-L pin 14 on OBD-II), bitrate (try 500000, then 250000) and that the ignition is on")
    return 0

if __name__ == "__main__":
    sys.exit(main())
