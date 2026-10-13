"""Boot metadata record (must match firmware/common/boot_meta.{h,c}) and the factory provisioning blob.

A board fresh from the flasher has no valid metadata, so the bootloader sees both slots EMPTY and boots nothing.
Provisioning writes one valid record that marks the first installed slot NEW; the bootloader then verifies the image
(signature, node, hw id) and boots it as TRIAL, and the app must confirm it like any OTA update.
"""
from __future__ import annotations

import struct

from .crc32c import crc32c

MAGIC = 0x4D455441            # "META"
STORED = 40                   # bytes written per copy (record + 0xFF padding)
STATUS = {"empty": 0, "new": 1, "trial": 2, "confirmed": 3, "bad": 4}

# Per board: (flash address of metadata copy A, copy B, erase unit). Mirrors firmware/boards/<board>/fb_board.h.
BOARDS = {
    "stm32g4": (0x08007000, 0x08007800, 2048),
    "mspm0g3507": (0x00007000, 0x00007800, 1024),
}

def build_record(slot: int, version: int, status: str = "new", min_version: int = 0, seq: int = 1) -> bytes:
    if slot not in (0, 1):
        raise ValueError("slot must be 0 (A) or 1 (B)")
    st = [STATUS["empty"], STATUS["empty"]]
    st[slot] = STATUS[status]
    ver = [0, 0]
    ver[slot] = version
    # magic, seq, min_version, version[2], status[2], attempts, active, reserved
    body = struct.pack("<IIIII4BI", MAGIC, seq, min_version, ver[0], ver[1], st[0], st[1], 0, slot, 0)
    rec = body + struct.pack("<I", crc32c(body))
    assert len(rec) == 32
    return rec + b"\xff" * (STORED - len(rec))

def provisioning_blob(board: str, slot: int, version: int, status: str = "new") -> tuple[int, bytes]:
    """(address, bytes) covering both metadata copies: copy A holds the record, everything else is 0xFF so a stale
    copy B from an earlier run can never outrank it (0xFF fails the magic/CRC check)."""
    a, b, page = BOARDS[board]
    blob = bytearray(b"\xff" * (b + page - a))
    blob[0:STORED] = build_record(slot, version, status)
    return a, bytes(blob)
