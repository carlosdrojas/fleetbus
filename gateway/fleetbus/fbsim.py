"""ctypes bindings to the host simulator (firmware/host/fbsim.c): the REAL firmware node core, boot logic and flash
fake, driven from Python. Build it first: cmake -S firmware -B build/host -G Ninja && cmake --build build/host"""
from __future__ import annotations

import ctypes
import os
import sys
from pathlib import Path

from . import canmsg
from .image import NODES

ROOT = Path(__file__).resolve().parents[2]

def find_lib() -> Path:
    ext = "dylib" if sys.platform == "darwin" else "so"
    cands = [os.environ.get("FBSIM_LIB", ""), str(ROOT / "build" / "host" / "lib" / f"libfbsim.{ext}")]
    for c in cands:
        if c and Path(c).exists():
            return Path(c)
    raise FileNotFoundError("libfbsim not built: cmake -S firmware -B build/host -G Ninja && cmake --build build/host")

_lib = None

def lib():
    global _lib
    if _lib is None:
        L = ctypes.CDLL(str(find_lib()))
        P = ctypes.c_void_p
        L.sim_node_new.restype = P; L.sim_node_new.argtypes = [ctypes.c_int, ctypes.c_char_p]
        L.sim_node_free.argtypes = [P]
        L.sim_node_provision.argtypes = [P, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint32]
        L.sim_node_power_on.argtypes = [P, ctypes.c_uint32]
        L.sim_node_rx.argtypes = [P, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint8]
        L.sim_node_tick.argtypes = [P, ctypes.c_uint32, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32, ctypes.c_int]
        L.sim_node_pop_tx.argtypes = [P, ctypes.POINTER(ctypes.c_uint32), ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint8)]
        for n in ("alive", "state", "slot"):
            getattr(L, f"sim_node_{n}").argtypes = [P]
        for n in ("image_version", "min_version", "boots", "resets_by_wdg"):
            f = getattr(L, f"sim_node_{n}"); f.argtypes = [P]; f.restype = ctypes.c_uint32
        L.sim_node_slot_status.argtypes = [P, ctypes.c_int]
        for n in ("lights", "servo", "limp"):
            f = getattr(L, f"sim_node_{n}"); f.argtypes = [P]; f.restype = ctypes.c_uint8
        L.sim_node_flip_byte.argtypes = [P, ctypes.c_int, ctypes.c_uint32]
        _lib = L
    return _lib

STATE = {0: "BOOT", 1: "NORMAL", 2: "WARNING", 3: "FAULT", 4: "SAFE", 5: "LIMP_HOME", 6: "UPDATING"}
SLOT_STATUS = {0: "EMPTY", 1: "NEW", 2: "TRIAL", 3: "CONFIRMED", 4: "BAD"}

class SimNode:
    def __init__(self, node_id: int, pubkey: bytes):
        self.id = node_id
        self.h = lib().sim_node_new(node_id, pubkey)
        self.sensors = dict(mv=3800, ma=500, dc=250, ok=1)
        self.powered = True       # False = unplugged from the CAN bus / not ticking (bench fault injection)

    def provision(self, slot: int, blob: bytes) -> None:
        rc = lib().sim_node_provision(self.h, slot, blob, len(blob))
        if rc != 0:
            raise RuntimeError(f"provision failed ({rc})")

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

    alive = property(lambda s: bool(lib().sim_node_alive(s.h)))
    state = property(lambda s: lib().sim_node_state(s.h))
    state_name = property(lambda s: STATE.get(lib().sim_node_state(s.h), "DOWN"))
    slot = property(lambda s: lib().sim_node_slot(s.h))
    version = property(lambda s: lib().sim_node_image_version(s.h))
    min_version = property(lambda s: lib().sim_node_min_version(s.h))
    boots = property(lambda s: lib().sim_node_boots(s.h))
    wdg_resets = property(lambda s: lib().sim_node_resets_by_wdg(s.h))
    limp_home = property(lambda s: bool(lib().sim_node_limp(s.h)))
    lights = property(lambda s: lib().sim_node_lights(s.h))
    servo = property(lambda s: lib().sim_node_servo(s.h))
    def slot_status(self, slot): return SLOT_STATUS[lib().sim_node_slot_status(self.h, slot)]
    def corrupt(self, slot, offset): lib().sim_node_flip_byte(self.h, slot, offset)
    def close(self):
        if self.h:
            lib().sim_node_free(self.h); self.h = None

class SimFleet:
    """A battery node and a body node on a virtual CAN bus. Frames sent by the gateway are delivered to all nodes;
    frames the nodes send are returned from step() for the gateway to forward."""
    def __init__(self, pubkey: bytes, images: dict[int, bytes], now_ms: int = 1000):
        self.nodes = {nid: SimNode(nid, pubkey) for nid in (1, 2)}
        for nid, blob in images.items():
            self.nodes[nid].provision(0, blob)
        self.now = now_ms
        for n in self.nodes.values():
            n.power_on(self.now)

    def set_power(self, node_id: int, on: bool) -> None:
        """Remove/restore power. Restoring power is a real boot: the bootloader runs and RAM state (FSMs) starts clean."""
        n = self.nodes[node_id]
        if on and not n.powered:
            n.powered = True
            n.power_on(self.now)
        elif not on:
            n.powered = False

    def inject(self, can_id: int, data: bytes) -> None:
        for n in self.nodes.values():
            if n.powered:
                n.rx(self.now, can_id, data)

    def step(self, now_ms: int) -> list[tuple[int, int, bytes]]:
        """Advance to now_ms in 10 ms ticks. Returns [(timestamp_ms, can_id, data)]."""
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
