#!/usr/bin/env python3
"""MOCK(M2): will open the CANable in listen-only (silent) mode and log every frame the Sentra emits.
Rule: never transmit. The interface is opened with state=SILENT and has no send path."""
import sys

def main() -> int:
    print("MOCK(M2): listen-only logger not implemented")
    return 0

if __name__ == "__main__":
    sys.exit(main())
