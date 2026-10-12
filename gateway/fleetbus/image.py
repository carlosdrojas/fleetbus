"""Signed firmware image format (must match firmware/common/image.h). Used by tools/sign_image.py, the simulator and tests."""
from __future__ import annotations

import hashlib
import struct

from nacl.signing import SigningKey, VerifyKey

MAGIC = 0x4D494246
HW_G431 = 0x4301
HDR_SIZE = 0x200
SIGNED_LEN = 0x60
SLOT_SIZE = 48 * 1024
NODES = {"battery": 1, "body": 2, "telematics": 3}

def build_image(body: bytes, node: int, slot: int, version: int, key: SigningKey, hw_id: int = HW_G431) -> bytes:
    if len(body) == 0 or len(body) > SLOT_SIZE - HDR_SIZE:
        raise ValueError(f"image body {len(body)} B does not fit a slot ({SLOT_SIZE - HDR_SIZE} B max)")
    h = bytearray(b"\xff" * HDR_SIZE)
    h[0:0x20] = struct.pack("<IHBBII", MAGIC, hw_id, slot, 1, version, len(body)) + bytes([node]) + b"\x00" * 15
    h[0x20:0x60] = hashlib.sha512(body).digest()
    h[0x60:0xA0] = key.sign(bytes(h[:SIGNED_LEN])).signature
    return bytes(h) + body

def parse_header(blob: bytes) -> dict:
    magic, hw, slot, hv, version, size = struct.unpack_from("<IHBBII", blob)
    return {"magic_ok": magic == MAGIC, "hw_id": hw, "slot": slot, "hdr_version": hv, "version": version, "size": size,
            "node": blob[0x10], "sha512": blob[0x20:0x60].hex(), "sig": blob[0x60:0xA0].hex()}

def verify_image(blob: bytes, pub: VerifyKey) -> None:
    h = parse_header(blob)
    pub.verify(blob[:SIGNED_LEN], blob[0x60:0xA0])
    if hashlib.sha512(blob[HDR_SIZE:HDR_SIZE + h["size"]]).digest() != blob[0x20:0x60]:
        raise ValueError("body hash mismatch")

def with_slot(blob: bytes, slot: int, key: SigningKey) -> bytes:
    """Re-sign the same application bytes for the other slot (a real build links the app separately per slot)."""
    h = parse_header(blob)
    return build_image(blob[HDR_SIZE:HDR_SIZE + h["size"]], h["node"], slot, h["version"], key, h["hw_id"])
