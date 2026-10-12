"""MOCK(M5): FBP framing. Header layout follows protocol/spec.md; CRC is a placeholder (zlib crc32, NOT CRC-32C)."""
import struct
import zlib

MAGIC = b"\xfb\x01"
HEADER = struct.Struct("<2sBBBIH")  # magic, ver, type, src, seq, len

def encode(ptype: int, src: int, seq: int, payload: bytes = b"") -> bytes:
    head = HEADER.pack(MAGIC, 1, ptype, src, seq, len(payload)) + payload
    return head + struct.pack("<I", zlib.crc32(head))

def decode(buf: bytes) -> dict:
    head, crc = buf[:-4], struct.unpack("<I", buf[-4:])[0]
    if zlib.crc32(head) != crc:
        raise ValueError("bad crc")
    magic, ver, ptype, src, seq, n = HEADER.unpack(head[:HEADER.size])
    return {"type": ptype, "src": src, "seq": seq, "payload": head[HEADER.size:HEADER.size + n]}
