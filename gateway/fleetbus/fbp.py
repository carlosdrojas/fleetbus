"""FBP v1 codec. See protocol/spec.md. Must stay byte-compatible with backend/internal/fbp (golden vectors)."""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from .crc32c import crc32c

MAGIC = b"\xfb\xb5"
VERSION = 1
HEADER = struct.Struct("<2sBBBBIH")   # magic, ver, type, src, flags, seq, len  (12 bytes)
MAX_DATAGRAM = 1200
MAX_PAYLOAD = MAX_DATAGRAM - HEADER.size - 4
FLAG_RETRANSMIT = 0x01

TELEMETRY, HEARTBEAT, ACK, NACK, OTA_OFFER, OTA_CHUNK, OTA_STATUS = range(1, 8)
OTA_ACCEPTED, OTA_PROGRESS, OTA_TRANSFERRED, OTA_FAILED, OTA_VERIFY_FAILED, OTA_CONFIRMED, OTA_ROLLED_BACK = range(7)

class FbpError(ValueError):
    pass

@dataclass(frozen=True)
class Packet:
    type: int
    src: int
    seq: int
    payload: bytes = b""
    flags: int = 0

def encode(p: Packet) -> bytes:
    if len(p.payload) > MAX_PAYLOAD:
        raise FbpError("payload too large")
    if not (1 <= p.type <= 7) or not (0 <= p.src <= 255) or not (0 <= p.seq <= 0xFFFFFFFF) or p.flags & ~FLAG_RETRANSMIT:
        raise FbpError("bad header field")
    head = HEADER.pack(MAGIC, VERSION, p.type, p.src, p.flags, p.seq, len(p.payload)) + p.payload
    return head + struct.pack("<I", crc32c(head))

def decode(buf: bytes) -> Packet:
    if len(buf) < HEADER.size + 4 or len(buf) > MAX_DATAGRAM:
        raise FbpError("bad size")
    magic, ver, ptype, src, flags, seq, n = HEADER.unpack_from(buf)
    if magic != MAGIC:
        raise FbpError("bad magic")
    if ver != VERSION:
        raise FbpError("bad version")
    if HEADER.size + n + 4 != len(buf):
        raise FbpError("length mismatch")
    if n > MAX_PAYLOAD:
        raise FbpError("payload too large")
    if struct.unpack_from("<I", buf, len(buf) - 4)[0] != crc32c(buf[:-4]):
        raise FbpError("bad crc")
    if flags & ~FLAG_RETRANSMIT:
        raise FbpError("reserved flags set")
    if not (1 <= ptype <= 7):
        raise FbpError("unknown type")
    return Packet(ptype, src, seq, bytes(buf[HEADER.size:HEADER.size + n]), flags)

def set_retransmit(wire: bytes) -> bytes:
    """Return the same packet with the retransmit flag set and the CRC recomputed."""
    head = bytearray(wire[:-4])
    head[5] |= FLAG_RETRANSMIT
    return bytes(head) + struct.pack("<I", crc32c(bytes(head)))

# ---- payloads ---------------------------------------------------------------------------------

@dataclass(frozen=True)
class CanRecord:
    dt_ms: int
    can_id: int          # bit31 = extended id
    data: bytes

def enc_telemetry(base_ts_ms: int, records: list[CanRecord]) -> bytes:
    out = bytearray(struct.pack("<QB", base_ts_ms, len(records)))
    for r in records:
        if len(r.data) > 8:
            raise FbpError("dlc > 8")
        out += struct.pack("<HIB", r.dt_ms, r.can_id, len(r.data)) + r.data
    return bytes(out)

def dec_telemetry(b: bytes) -> tuple[int, list[CanRecord]]:
    if len(b) < 9:
        raise FbpError("short telemetry")
    base, count = struct.unpack_from("<QB", b)
    off, recs = 9, []
    for _ in range(count):
        if off + 7 > len(b):
            raise FbpError("truncated record")
        dt, cid, dlc = struct.unpack_from("<HIB", b, off)
        off += 7
        if dlc > 8 or off + dlc > len(b):
            raise FbpError("bad dlc")
        recs.append(CanRecord(dt, cid, bytes(b[off:off + dlc])))
        off += dlc
    if off != len(b):
        raise FbpError("trailing bytes")
    return base, recs

def enc_heartbeat(uptime_s: int, unacked: int, spooled: int, resent: int) -> bytes:
    return struct.pack("<IHII", uptime_s, unacked, spooled, resent)

def dec_heartbeat(b: bytes) -> tuple[int, int, int, int]:
    if len(b) != 14:
        raise FbpError("bad heartbeat")
    return struct.unpack("<IHII", b)

def enc_ack(cum: int) -> bytes:
    return struct.pack("<I", cum)

def dec_ack(b: bytes) -> int:
    if len(b) != 4:
        raise FbpError("bad ack")
    return struct.unpack("<I", b)[0]

def enc_nack(seqs: list[int]) -> bytes:
    if len(seqs) > 255:
        raise FbpError("too many nacks")
    return struct.pack("<B", len(seqs)) + b"".join(struct.pack("<I", s) for s in seqs)

def dec_nack(b: bytes) -> list[int]:
    if not b or len(b) != 1 + 4 * b[0]:
        raise FbpError("bad nack")
    return list(struct.unpack_from("<%dI" % b[0], b, 1))

def enc_ota_offer(node: int, slot: int, version: int, size: int, image_id: int) -> bytes:
    return struct.pack("<BBIII", node, slot, version, size, image_id)

def dec_ota_offer(b: bytes) -> tuple[int, int, int, int, int]:
    if len(b) != 14:
        raise FbpError("bad offer")
    return struct.unpack("<BBIII", b)

def enc_ota_chunk(node: int, offset: int, data: bytes) -> bytes:
    return struct.pack("<BI", node, offset) + data

def dec_ota_chunk(b: bytes) -> tuple[int, int, bytes]:
    if len(b) < 5:
        raise FbpError("bad chunk")
    node, off = struct.unpack_from("<BI", b)
    return node, off, bytes(b[5:])

def enc_ota_status(node: int, status: int, next_offset: int, detail: int = 0) -> bytes:
    return struct.pack("<BBIH", node, status, next_offset, detail)

def dec_ota_status(b: bytes) -> tuple[int, int, int, int]:
    if len(b) != 8:
        raise FbpError("bad status")
    return struct.unpack("<BBIH", b)
