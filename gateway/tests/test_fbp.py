import json
import random
from pathlib import Path

import pytest

from fleetbus import fbp
from fleetbus.crc32c import crc32c

VEC = json.loads((Path(__file__).resolve().parents[2] / "protocol" / "golden_vectors.json").read_text())

def test_crc32c_check_value():
    assert crc32c(b"123456789") == 0xE3069283 == VEC["crc32c_check"]["crc"]
    assert crc32c(b"\x00" * 32) == 0x8A9136AA     # RFC 3720 B.4

@pytest.mark.parametrize("v", VEC["valid"], ids=lambda v: v["name"])
def test_golden_valid(v):
    p = fbp.Packet(v["type"], v["src"], v["seq"], bytes.fromhex(v["payload_hex"]), v["flags"])
    assert fbp.encode(p).hex() == v["wire_hex"]
    assert fbp.decode(bytes.fromhex(v["wire_hex"])) == p

@pytest.mark.parametrize("v", VEC["invalid"], ids=lambda v: v["name"])
def test_golden_invalid(v):
    with pytest.raises(fbp.FbpError):
        fbp.decode(bytes.fromhex(v["wire_hex"]))

def test_golden_payload_fields():
    by = {v["name"]: v for v in VEC["valid"]}
    t = by["telemetry_three_records"]
    base, recs = fbp.dec_telemetry(bytes.fromhex(t["payload_hex"]))
    assert base == t["fields"]["base_ts_ms"]
    assert [(r.dt_ms, r.can_id, r.data.hex()) for r in recs] == [(r["dt_ms"], r["can_id"], r["data_hex"]) for r in t["fields"]["records"]]
    assert fbp.dec_nack(bytes.fromhex(by["nack_two_gaps"]["payload_hex"])) == [44, 47]
    assert fbp.dec_ota_status(bytes.fromhex(by["ota_status_confirmed"]["payload_hex"]))[:2] == (2, fbp.OTA_CONFIRMED)
    assert fbp.dec_heartbeat(bytes.fromhex(by["gateway_heartbeat"]["payload_hex"])) == (3600, 12, 0, 5)
    assert fbp.dec_ota_offer(bytes.fromhex(by["ota_offer"]["payload_hex"])) == (2, 1, 3, 20480, 77)

def test_roundtrip_random_packets():
    rng = random.Random(1)
    for _ in range(500):
        p = fbp.Packet(rng.randint(1, 7), rng.randint(0, 255), rng.randint(0, 2**32 - 1),
                       bytes(rng.randrange(256) for _ in range(rng.randint(0, 300))), rng.choice([0, 1]))
        assert fbp.decode(fbp.encode(p)) == p

def test_decoder_only_raises_fbperror_on_garbage():
    rng = random.Random(2)
    good = fbp.encode(fbp.Packet(fbp.TELEMETRY, 1, 5, fbp.enc_telemetry(1, [fbp.CanRecord(0, 0x100, b"\x01")])))
    for _ in range(2000):
        b = bytearray(good)
        for _ in range(rng.randint(1, 4)):
            b[rng.randrange(len(b))] = rng.randrange(256)
        try:
            fbp.decode(bytes(b))
        except fbp.FbpError:
            pass

def test_single_bit_flips_always_detected():
    wire = fbp.encode(fbp.Packet(fbp.TELEMETRY, 1, 5, fbp.enc_telemetry(1, [fbp.CanRecord(0, 0x100, b"\x01\x02")])))
    for i in range(len(wire) * 8):
        b = bytearray(wire); b[i // 8] ^= 1 << (i % 8)
        with pytest.raises(fbp.FbpError):
            fbp.decode(bytes(b))

def test_size_limits():
    with pytest.raises(fbp.FbpError):
        fbp.encode(fbp.Packet(fbp.ACK, 0, 1, b"x" * (fbp.MAX_PAYLOAD + 1)))
    assert len(fbp.encode(fbp.Packet(fbp.OTA_CHUNK, 0, 1, b"x" * fbp.MAX_PAYLOAD))) == fbp.MAX_DATAGRAM

def test_telemetry_rejects_bad_payloads():
    for bad in (b"", b"\x00" * 8, fbp.enc_telemetry(0, [])[:-1], fbp.enc_telemetry(0, []) + b"x"):
        with pytest.raises(fbp.FbpError):
            fbp.dec_telemetry(bad)
