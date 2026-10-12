import os
import random

import pytest

from fleetbus import uartlink
from fleetbus.canio import SerialBackend

try:
    from fleetbus.fbsim import c_link_encode, lib
    lib()
    HAVE_SIM = True
except FileNotFoundError:
    HAVE_SIM = False

def test_crc8_check_value():
    assert uartlink.crc8(b"123456789") == 0xF4

def test_roundtrip_including_special_bytes():
    d = uartlink.Decoder()
    data = bytes([0x7E, 0x7D, 0x00, 0xFF, 0x7E, 0x20, 0x5E, 0x5D])
    assert d.feed(uartlink.encode(0x7DF, data) + bytes([uartlink.SOF])) == [(0x7DF, data)]

def test_stream_split_at_every_byte_boundary():
    frames = [(0x100, b"\x01\x02"), (0x7E8, bytes([3, 0x41, 0x0D, 88, 0x55, 0x55, 0x55, 0x55])), (0x500, b"")]
    wire = b"".join(uartlink.encode(i, x) for i, x in frames) + bytes([uartlink.SOF])
    d, got = uartlink.Decoder(), []
    for b in wire:
        got += d.feed(bytes([b]))
    assert got == frames

def test_corruption_is_dropped_and_stream_resyncs():
    good = uartlink.encode(0x200, b"abc")
    bad = bytearray(good); bad[-1] ^= 1
    d = uartlink.Decoder()
    out = d.feed(bytes(bad) + good + bytes([uartlink.SOF]))
    assert out == [(0x200, b"abc")] and d.crc_errors == 1

def test_random_noise_yields_no_frames():
    rng = random.Random(1)
    d = uartlink.Decoder()
    assert len(d.feed(bytes(rng.randrange(256) for _ in range(200000)))) < 5

@pytest.mark.skipif(not HAVE_SIM, reason="host build missing")
def test_python_encoder_matches_firmware_encoder_byte_for_byte():
    rng = random.Random(7)
    for _ in range(300):
        cid = rng.randrange(0x800)
        data = bytes(rng.choice([0x7E, 0x7D, 0x20, rng.randrange(256)]) for _ in range(rng.randint(0, 8)))
        assert uartlink.encode(cid, data) == c_link_encode(cid, data)

class FakeSerial:
    def __init__(self, chunks): self.chunks, self.written = list(chunks), b""
    def read(self, n): return self.chunks.pop(0) if self.chunks else b""
    def write(self, b): self.written += b

def test_serial_backend_yields_frames_and_refuses_to_transmit():
    wire = uartlink.encode(0x7E8, b"\x03\x41\x0d\x58") + uartlink.encode(0x100, b"\x01") + bytes([uartlink.SOF])
    be = SerialBackend("fake", serial_obj=FakeSerial([wire[:7], wire[7:], b""]))
    got = be.poll(10) + be.poll(20) + be.poll(30)
    assert [(c, d) for _, c, d in got] == [(0x7E8, b"\x03\x41\x0d\x58"), (0x100, b"\x01")]
    for forbidden in (0x7DF, 0x7E0, 0x100, 0x402):     # OBD requests, ECU ids, anything but OTA-to-node
        with pytest.raises(PermissionError):
            be.send(forbidden, b"\x02\x01\x0c")
    be.send(0x400, b"\x01\x00\x00\x00\x00\x00\x00\x01")           # OTA control to the telematics node is allowed
    assert be.ser.written and be.ser.written[0] == uartlink.SOF

def test_serial_backend_over_a_real_pty():
    import termios  # noqa: F401
    master, slave = os.openpty()
    import serial
    ser = serial.Serial(os.ttyname(slave), 115200, timeout=0)
    be = SerialBackend("pty", serial_obj=ser)
    os.write(master, uartlink.encode(0x500, b"\x58\x00\x2b\x82\x00\x00\x00\x01") + bytes([uartlink.SOF]))
    import time
    got = []
    for _ in range(50):
        got += be.poll(0)
        if got: break
        time.sleep(0.02)
    assert got and got[0][1] == 0x500
