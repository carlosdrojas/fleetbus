#!/usr/bin/env python3
"""Render the body-node instrument cluster with the firmware's own renderer (build/host/host/gauge_shot) and save
PNGs to docs/img/ (stdlib only: no Pillow).
  tools/gauge_shots.py [scale]                 default scale 3 = 480x384
  tools/gauge_shots.py 3 --embed docs/mspm0-bench.html   also inline them into the 3D bench page (REAL_LCD marker)"""
import base64
import re
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def png(path: Path, w: int, h: int, rgb: bytes) -> None:
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

def main() -> int:
    args = [a for a in sys.argv[1:] if a != "--embed"]
    embed = Path(sys.argv[sys.argv.index("--embed") + 1]) if "--embed" in sys.argv else None
    if embed:
        args.remove(str(sys.argv[sys.argv.index("--embed") + 1]))
    scale = int(args[0]) if args else 3
    tool = ROOT / "build" / "host" / "host" / "gauge_shot"
    out = ROOT / "docs" / "img"
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for line in subprocess.run([str(tool), tmp], check=True, capture_output=True, text=True).stdout.split():
            p = Path(line)
            data = p.read_bytes()
            header, pix = data.split(b"\n255\n", 1)
            w, h = map(int, header.split()[1:3])
            big = bytearray()
            for y in range(h):
                row = b"".join(pix[(y * w + x) * 3:(y * w + x) * 3 + 3] * scale for x in range(w))
                big += row * scale
            dst = out / (p.stem + ".png")
            png(dst, w * scale, h * scale, bytes(big))
            print(dst.relative_to(ROOT))
    if embed:
        uris = {p.stem.replace("lcd_", ""): "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode()
                for p in sorted(out.glob("lcd_*.png"))}
        page = embed.read_text()
        page, n = re.subn(r"/\*REAL_LCD\*/\{.*?\}/\*END\*/", "/*REAL_LCD*/" + json_dumps(uris) + "/*END*/", page, flags=re.S)
        if n != 1:
            print("REAL_LCD marker not found in", embed, file=sys.stderr)
            return 1
        embed.write_text(page)
        print("embedded", len(uris), "images in", embed)
    return 0

def json_dumps(d):
    import json
    return json.dumps(d)

if __name__ == "__main__":
    sys.exit(main())
