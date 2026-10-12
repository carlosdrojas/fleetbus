"""Serial link to the in-car telematics node: CAN frames over a UART/USB-CDC byte stream (see firmware/common/uart_link.h).
  0x7E | id_lo id_hi dlc data crc8 | with 0x7E/0x7D escaped (0x7D, b ^ 0x20); CRC-8 poly 0x07 over id..data."""
from __future__ import annotations

SOF, ESC = 0x7E, 0x7D

def crc8(data: bytes) -> int:
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def encode(can_id: int, data: bytes) -> bytes:
    if len(data) > 8:
        raise ValueError("dlc > 8")
    body = bytes([can_id & 0xFF, (can_id >> 8) & 0x07, len(data)]) + data
    body += bytes([crc8(body)])
    out = bytearray([SOF])
    for b in body:
        out += bytes([ESC, b ^ 0x20]) if b in (SOF, ESC) else bytes([b])
    return bytes(out)

class Decoder:
    def __init__(self):
        self.buf = bytearray()
        self.esc = False
        self.in_frame = False
        self.crc_errors = 0
        self.framing_errors = 0

    def feed(self, chunk: bytes) -> list[tuple[int, bytes]]:
        out = []
        for b in chunk:
            if b == SOF:
                if self.in_frame and len(self.buf) >= 4:
                    dlc = self.buf[2]
                    if dlc <= 8 and len(self.buf) == 4 + dlc and crc8(bytes(self.buf[:-1])) == self.buf[-1]:
                        out.append((self.buf[0] | ((self.buf[1] & 7) << 8), bytes(self.buf[3:3 + dlc])))
                    else:
                        self.crc_errors += 1
                elif self.in_frame and self.buf:
                    self.framing_errors += 1
                self.buf.clear()
                self.esc = False
                self.in_frame = True
            elif not self.in_frame:
                continue
            elif b == ESC:
                self.esc = True
            else:
                if self.esc:
                    b ^= 0x20
                    self.esc = False
                if len(self.buf) >= 16:
                    self.framing_errors += 1
                    self.in_frame = False
                    self.buf.clear()
                else:
                    self.buf.append(b)
        return out
