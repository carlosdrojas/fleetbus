"""tools/sign_image.py (PyNaCl) and the firmware's C verifier (Monocypher) must agree, including on a real ARM binary."""
import os
import subprocess
import sys
from pathlib import Path

import pytest
from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gateway"))
from fleetbus import image as sign_image  # noqa: E402

VERIFY = Path(os.environ.get("FB_VERIFY", ROOT / "build" / "host" / "host" / "fb_verify"))
pytestmark = pytest.mark.skipif(not VERIFY.exists(), reason="host build missing: cmake -S firmware -B build/host && cmake --build build/host")

KEY = SigningKey(bytes(range(32)))
PUB = bytes(KEY.verify_key).hex()
BODY = bytes((i * 7 + 3) & 0xFF for i in range(5000))

def c_verify(tmp_path, blob, node=2, min_version=0, pub=PUB):
    p = tmp_path / "img.signed"
    p.write_bytes(blob)
    r = subprocess.run([str(VERIFY), pub, str(p), str(node), str(min_version)], capture_output=True, text=True)
    return r.returncode, r.stdout

def test_python_signed_image_verifies_in_c(tmp_path):
    rc, out = c_verify(tmp_path, sign_image.build_image(BODY, 2, 0, 7, KEY))
    assert rc == 0, out
    assert "version=7" in out

def test_other_slot_header(tmp_path):
    assert c_verify(tmp_path, sign_image.build_image(BODY, 2, 1, 7, KEY))[0] == 0

@pytest.mark.parametrize("name,mutate,code", [
    ("body_bit_flip", lambda b: b[:2000] + bytes([b[2000] ^ 1]) + b[2001:], 6),
    ("version_edit", lambda b: b[:8] + bytes([99]) + b[9:], 7),
    ("hash_edit", lambda b: b[:0x20] + bytes([b[0x20] ^ 1]) + b[0x21:], 7),
    ("signature_zeroed", lambda b: b[:0x60] + bytes(64) + b[0xA0:], 7),
    ("magic_wrong", lambda b: b"\x00" + b[1:], 2),
])
def test_tampering_rejected_with_expected_code(tmp_path, name, mutate, code):
    blob = sign_image.build_image(BODY, 2, 0, 7, KEY)
    rc, out = c_verify(tmp_path, mutate(blob))
    assert rc == code, (name, out)

def test_wrong_key_node_and_downgrade_rejected(tmp_path):
    blob = sign_image.build_image(BODY, 2, 0, 7, KEY)
    assert c_verify(tmp_path, blob, pub=bytes(SigningKey(bytes(32)).verify_key).hex())[0] == 7
    assert c_verify(tmp_path, blob, node=1)[0] == 9
    assert c_verify(tmp_path, blob, min_version=8)[0] == 8
    assert c_verify(tmp_path, blob, min_version=7)[0] == 0

def test_real_arm_binary_roundtrip(tmp_path):
    binp = ROOT / "build" / "fw" / "arm" / "app_body_A.elf.bin"
    if not binp.exists():
        pytest.skip("ARM build missing")
    blob = sign_image.build_image(binp.read_bytes(), 2, 0, 1, KEY)
    assert len(blob) <= sign_image.SLOT_SIZE
    assert c_verify(tmp_path, blob)[0] == 0
