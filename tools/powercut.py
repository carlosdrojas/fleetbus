#!/usr/bin/env python3
"""Power-cut torture test for the bootloader's metadata writes (P6). BENCH ONLY, desk nodes only.

Every boot rewrites the boot metadata (attempt counter), and an OTA confirm rewrites it again. The host tests tear
every flash erase and write in the fake flash (firmware/tests/test_boot_meta.c); this is the same idea on a real board:
cut power at a random moment in the first seconds after power-on, many times, and check the node always comes back.

You need a way to switch the node's power from a shell command, for example a USB relay board or a smart plug:
  tools/powercut.py --port /dev/tty.usbmodemXXXX1 --on "usbrelay R1_1=1" --off "usbrelay R1_1=0" --cycles 200
Pass:   every final power-on prints the app banner ("node N fw vV slot X ...") within --boot-timeout seconds.
Record: cycles run, failures (with the cut delay that preceded each), and whether the slot/version ever changed.
The XDS110 must not power the target during this test (remove the 3V3 jumper and power the LaunchPad externally
through the relay), or the cut does nothing. NOT YET RUN: written before the hardware arrived.
"""
import argparse
import random
import re
import subprocess
import sys
import time

import serial

BANNER = re.compile(rb"node (\d+) fw v(\d+) slot ([AB]) (TRIAL|confirmed)")

def sh(cmd: str) -> None:
    subprocess.run(cmd, shell=True, check=True)

def wait_banner(port: serial.Serial, timeout: float):
    end, buf = time.monotonic() + timeout, b""
    while time.monotonic() < end:
        buf += port.read(256)
        m = BANNER.search(buf)
        if m:
            return m.groups()
    return None

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="node log UART (XDS110 backchannel / ST-LINK VCP)")
    ap.add_argument("--on", required=True, help="shell command that powers the node")
    ap.add_argument("--off", required=True, help="shell command that cuts the node's power")
    ap.add_argument("--cycles", type=int, default=100)
    ap.add_argument("--max-cut-ms", type=int, default=1500, help="cut uniformly in [0, this] ms after power-on")
    ap.add_argument("--boot-timeout", type=float, default=5.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    port = serial.Serial(a.port, 115200, timeout=0.05)
    # A TRIAL slot counts every boot as an attempt and is marked BAD after 3 (by design), so cutting power before the
    # app confirms would roll back a freshly provisioned board. Require a confirmed image first.
    sh(a.on); time.sleep(15); sh(a.off); time.sleep(0.5); sh(a.on); port.reset_input_buffer()
    seen = wait_banner(port, a.boot_timeout)
    if not seen or seen[3] != b"confirmed":
        print(f"precheck: expected a confirmed image after a 15 s run, saw {seen}; provision and let it confirm first")
        return 2
    sh(a.off); time.sleep(0.3)
    first, failures = None, []
    for i in range(a.cycles):
        delay = rnd.uniform(0, a.max_cut_ms) / 1000
        sh(a.on); time.sleep(delay); sh(a.off); time.sleep(0.3)
        sh(a.on); port.reset_input_buffer()
        seen = wait_banner(port, a.boot_timeout)
        first = first or seen
        status = "ok" if seen else "NO BOOT"
        if not seen:
            failures.append((i, round(delay * 1000)))
        print(f"cycle {i + 1}/{a.cycles} cut at {delay * 1000:6.0f} ms -> {status} {seen or ''}", flush=True)
        sh(a.off); time.sleep(0.3)
    print(f"\n{a.cycles - len(failures)}/{a.cycles} came back; first banner {first}; failures (cycle, cut ms): {failures}")
    return 1 if failures else 0

if __name__ == "__main__":
    sys.exit(main())
