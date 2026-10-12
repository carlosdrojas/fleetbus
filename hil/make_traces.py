#!/usr/bin/env python3
"""Regenerate hil/traces/*.log (candump -L format) as synthetic battery-node traces. Replace/add real captures from the
bench with `candump -L can0 > hil/traces/<name>.log`; the tests only care about the frames."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from hil import TRACES, battery_frames, write_trace

write_trace(TRACES / "battery_normal.log", battery_frames(4))
write_trace(TRACES / "battery_fault_at_2s.log", battery_frames(2, state=1) + [f.__class__(f.t + 2, f.can_id, f.data) for f in battery_frames(3, state=3, dc=700)])
write_trace(TRACES / "battery_dies_at_1s.log", battery_frames(4, silent_after=1.0))
print("wrote 3 traces to", TRACES)
