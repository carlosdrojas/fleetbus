"""ctypes bindings to the host simulator (firmware/host/fbsim.cpp): the REAL firmware node logic, driven from
Python with no hardware. Build it first: make host-test."""
from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
STATE = {0: "BOOT", 1: "NORMAL", 2: "WARNING", 3: "FAULT", 4: "SAFE", 5: "LIMP_HOME"}


def find_lib() -> Path:
    ext = "dylib" if sys.platform == "darwin" else "so"
    for c in (os.environ.get("FBSIM_LIB", ""), str(ROOT / "build" / "host" / "lib" / f"libfbsim.{ext}")):
        if c and Path(c).exists():
            return Path(c)
    raise FileNotFoundError("libfbsim not built: make host-test")


_lib = None


def lib():
    global _lib
    if _lib is None:
        L = ctypes.CDLL(str(find_lib()))
        P = ctypes.c_void_p
        L.sim_node_new.restype = P
        L.sim_node_new.argtypes = [ctypes.c_int]
        L.sim_node_free.argtypes = [P]
        L.sim_node_power_on.argtypes = [P, ctypes.c_uint32]
        L.sim_node_rx.argtypes = [P, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint8]
        L.sim_node_tick.argtypes = [P, ctypes.c_uint32, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32, ctypes.c_int]
        L.sim_node_pop_tx.argtypes = [P, ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8)]
        L.sim_node_state.argtypes = [P]
        for n in ("lights", "servo", "limp"):
            f = getattr(L, f"sim_node_{n}")
            f.argtypes = [P]
            f.restype = ctypes.c_uint8
        _lib = L
    return _lib


class SimNode:
    def __init__(self, node_id: int):
        self.id = node_id
        self.h = lib().sim_node_new(node_id)
        self.sensors = dict(mv=3800, ma=500, dc=250, ok=1)
        self.powered = True       # False = unplugged from the bus and not ticking (bench fault injection)

    def power_on(self, now): lib().sim_node_power_on(self.h, now)
    def rx(self, now, can_id, data: bytes): lib().sim_node_rx(self.h, now, can_id, data, len(data))

    def tick(self, now):
        s = self.sensors
        lib().sim_node_tick(self.h, now, s["mv"], s["ma"], s["dc"], s["ok"])

    def pop_tx(self):
        i, d, n = ctypes.c_uint32(), ctypes.create_string_buffer(8), ctypes.c_uint8()
        out = []
        while lib().sim_node_pop_tx(self.h, ctypes.byref(i), d, ctypes.byref(n)):
            out.append((i.value, d.raw[:n.value]))
        return out

    state = property(lambda s: lib().sim_node_state(s.h))
    state_name = property(lambda s: STATE.get(lib().sim_node_state(s.h), "DOWN"))
    limp_home = property(lambda s: bool(lib().sim_node_limp(s.h)))
    lights = property(lambda s: lib().sim_node_lights(s.h))
    servo = property(lambda s: lib().sim_node_servo(s.h))

    def close(self):
        if self.h:
            lib().sim_node_free(self.h)
            self.h = None


class SimFleet:
    """A battery node and a body node on a virtual CAN bus, stepped in 10 ms ticks like the node task. Frames one
    node sends reach the other node in the same tick, and are returned from step() so the dashboard sees the bus."""

    def __init__(self, now_ms: int = 1000):
        self.nodes = {nid: SimNode(nid) for nid in (1, 2)}
        self.now = now_ms
        for n in self.nodes.values():
            n.power_on(self.now)

    def set_power(self, node_id: int, on: bool) -> None:
        """Remove or restore power. Restoring power is a fresh boot: every FSM starts clean."""
        n = self.nodes[node_id]
        if on and not n.powered:
            n.powered = True
            n.power_on(self.now)
        elif not on:
            n.powered = False

    def step(self, now_ms: int) -> list[tuple[int, int, bytes]]:
        """Advance to now_ms. Returns [(timestamp_ms, can_id, data)] for every frame put on the bus."""
        out = []
        while self.now + 10 <= now_ms:
            self.now += 10
            for n in self.nodes.values():
                if n.powered:
                    n.tick(self.now)
            for n in self.nodes.values():
                if not n.powered:
                    continue
                for cid, data in n.pop_tx():
                    out.append((self.now, cid, data))
                    for o in self.nodes.values():
                        if o is not n and o.powered:
                            o.rx(self.now, cid, data)
        return out

    def close(self):
        for n in self.nodes.values():
            n.close()
