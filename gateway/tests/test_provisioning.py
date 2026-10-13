"""Factory provisioning: tools/provision_meta.py + a signed image must be booted by the C bootloader logic
(fb_boot_run via build/host*/host/fb_boot) on each board's flash map. Without provisioning nothing boots."""
import subprocess
import sys
from pathlib import Path

import pytest
from nacl.signing import SigningKey

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gateway"))
from fleetbus import bootmeta, image  # noqa: E402

KEY = SigningKey(bytes(range(32)))
PUB = bytes(KEY.verify_key).hex()
BODY = bytes((i * 13 + 1) & 0xFF for i in range(3000))
TOOLS = {"stm32g4": ROOT / "build" / "host" / "host" / "fb_boot",
         "mspm0g3507": ROOT / "build" / "host-mspm0" / "host" / "fb_boot"}

def boot(board, tmp_path, meta=None, images=()):
    tool = TOOLS[board]
    if not tool.exists():
        pytest.skip(f"{tool} missing: make host-test")
    args = [str(tool), PUB, "2"]
    addr, blob = meta if meta else (bootmeta.BOARDS[board][0], b"")
    mp = tmp_path / "meta.bin"
    mp.write_bytes(blob)
    args += [f"{addr:x}", str(mp)]
    for i, img in enumerate(images):
        p = tmp_path / f"img{i}.signed"
        p.write_bytes(img)
        args.append(str(p))
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, dict(kv.split("=") for kv in r.stdout.split())

@pytest.mark.parametrize("board", list(TOOLS))
def test_provisioned_board_boots_slot_a_as_trial(board, tmp_path):
    img = image.build_image(BODY, 2, 0, 5, KEY, image.BOARDS[board])
    rc, out = boot(board, tmp_path, bootmeta.provisioning_blob(board, 0, 5), [img])
    assert rc == 0, out
    assert out["board"] == board and out["slot"] == "0" and out["trial"] == "1" and out["version"] == "5"
    assert out["status"] == "2,0" and out["attempts"] == "1"      # TRIAL, first attempt counted and persisted

@pytest.mark.parametrize("board", list(TOOLS))
def test_unprovisioned_board_boots_nothing(board, tmp_path):
    img = image.build_image(BODY, 2, 0, 5, KEY, image.BOARDS[board])
    rc, out = boot(board, tmp_path, None, [img])
    assert rc == 1 and out["slot"] == "-1"

def test_provisioning_does_not_bypass_the_board_check(tmp_path):
    other = image.build_image(BODY, 2, 0, 5, KEY, image.HW_G431)     # STM32 image on an MSPM0 board
    rc, out = boot("mspm0g3507", tmp_path, bootmeta.provisioning_blob("mspm0g3507", 0, 5), [other])
    assert rc == 1 and out["status"].startswith("4")                 # slot A marked BAD, nothing booted

def test_provision_cli_prints_the_board_address(tmp_path):
    out = tmp_path / "m.bin"
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "provision_meta.py"), "--board", "mspm0g3507",
                        "--version", "3", "-o", str(out)], capture_output=True, text=True, check=True)
    assert r.stdout.strip() == "0x00007000"
    assert len(out.read_bytes()) == 0x7800 + 1024 - 0x7000
