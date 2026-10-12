"""HIL support: candump-format traces, and a Target abstraction so the SAME test runs against
  * SimTarget  - the host simulator (real firmware logic), in CI, and
  * BoardTarget - a real Nucleo on a CAN adapter (python-can), on the bench:  HIL_CAN=slcan:/dev/tty.usbmodemXXXX pytest hil

The Target plays the other nodes: a trace recorded from the battery node is replayed INTO the body node, and the
test asserts what the body node transmits and does. Replaying onto a real bus makes this adapter impersonate the battery
node, so it is a BENCH-ONLY mode (never point it at the car; the car-facing code has its own allowlist)."""
from __future__ import annotations

import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "gateway"))
from fleetbus import canmsg  # noqa: E402

TRACES = Path(__file__).resolve().parent / "traces"
LINE = re.compile(r"\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)")

@dataclass(frozen=True)
class Frame:
    t: float            # seconds from trace start
    can_id: int
    data: bytes

def write_trace(path: Path, frames: list[Frame], iface: str = "can0") -> None:
    path.write_text("".join(f"({f.t:.6f}) {iface} {f.can_id:03X}#{f.data.hex().upper()}\n" for f in frames))

def read_trace(path: Path) -> list[Frame]:
    out = []
    for line in path.read_text().splitlines():
        m = LINE.match(line.strip())
        if m:
            out.append(Frame(float(m.group(1)), int(m.group(3), 16), bytes.fromhex(m.group(4))))
    if out:
        t0 = out[0].t
        out = [Frame(f.t - t0, f.can_id, f.data) for f in out]
    return out

def decoded(frames: list[Frame], name: str) -> list[tuple[float, dict]]:
    res = []
    for f in frames:
        d = canmsg.decode(f.can_id, f.data)
        if d and d[0] == name:
            res.append((f.t, d[1]))
    return res

class Target:
    """The device under test is the BODY node (node id 2). `play` injects frames as the peer(s); `tx` returns what the
    DUT transmitted during the run."""
    def play(self, frames: list[Frame], extra_s: float = 0.5) -> list[Frame]:
        raise NotImplementedError

class SimTarget(Target):
    def __init__(self):
        from nacl.signing import SigningKey
        from fleetbus.fbsim import SimNode
        from fleetbus.image import build_image
        self.key = SigningKey(bytes(range(32)))
        self.node = SimNode(2, bytes(self.key.verify_key))
        self.node.provision(0, build_image(bytes([0, 1]) + b"D" * 2000, 2, 0, 1, self.key))
        self.node.power_on(1000)
        self.t0 = 1000

    def play(self, frames, extra_s=0.5):
        out, now, i = [], self.t0, 0
        end = (frames[-1].t if frames else 0) + extra_s
        t = 0.0
        while t <= end:
            while i < len(frames) and frames[i].t <= t:
                self.node.rx(now, frames[i].can_id, frames[i].data)
                i += 1
            self.node.tick(now)
            for cid, data in self.node.pop_tx():
                out.append(Frame(t, cid, data))
            t += 0.01
            now = self.t0 + int(round(t * 1000))
        self.t0 = now
        return out

class BoardTarget(Target):
    def __init__(self, spec: str, bitrate: int = 500000):
        import can
        iface, channel = spec.split(":", 1)
        self.bus = can.Bus(interface=iface, channel=channel, bitrate=bitrate)

    def play(self, frames, extra_s=0.5):
        import can
        out, start, i = [], time.monotonic(), 0
        end = (frames[-1].t if frames else 0) + extra_s
        while True:
            t = time.monotonic() - start
            while i < len(frames) and frames[i].t <= t:
                self.bus.send(can.Message(arbitration_id=frames[i].can_id, data=frames[i].data, is_extended_id=False))
                i += 1
            m = self.bus.recv(timeout=0.002)
            if m is not None and m.arbitration_id in (canmsg.HEARTBEAT, canmsg.BODY_STATUS, canmsg.FAULT_EVENT):
                out.append(Frame(t, m.arbitration_id, bytes(m.data)))
            if t > end:
                return out

def make_target() -> Target:
    spec = os.environ.get("HIL_CAN")
    return BoardTarget(spec) if spec else SimTarget()

def battery_frames(seconds: float, *, state: int = 1, mv=3800, dc=250, node_id=1, silent_after: float | None = None, hz=10) -> list[Frame]:
    """What a battery node puts on the bus: Heartbeat + BatteryStatus at 10 Hz, optionally dying at `silent_after`."""
    out, n = [], int(seconds * hz)
    for k in range(n):
        t = k / hz
        if silent_after is not None and t >= silent_after:
            break
        cid, d = canmsg.encode("Heartbeat", NodeId=node_id, FwVersion=1, UptimeS=int(t), State=state, Slot=0, Trial=0)
        out.append(Frame(t, cid, d))
        cid, d = canmsg.encode("BatteryStatus", PackMilliVolt=mv, CurrentMilliAmp=500, TempDeciC=dc, SocPct=66)
        out.append(Frame(t, cid, d))
    return out
