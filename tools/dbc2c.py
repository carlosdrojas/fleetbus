#!/usr/bin/env python3
"""MOCK(M1): will generate firmware/common/can_msgs.{h,c} from can/fleetbus.dbc via cantools."""
import sys

def main() -> int:
    print("MOCK(M1): dbc2c not implemented; firmware/common/can_msgs.* are hand-written stand-ins")
    return 0

if __name__ == "__main__":
    sys.exit(main())
