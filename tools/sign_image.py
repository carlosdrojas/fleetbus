#!/usr/bin/env python3
"""MOCK(M10): will build the image header (magic, hw id, slot, version, size, SHA-512) and an Ed25519 signature (PyNaCl).
The private key lives in ~/.fleetbus/keys, never in the repo."""
import sys

def main() -> int:
    print("MOCK(M10): signing not implemented; no image is signed")
    return 1

if __name__ == "__main__":
    sys.exit(main())
