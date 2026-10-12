"""HIL tests for the body node. Default target: simulator (real firmware logic). Real board: HIL_CAN=interface:channel."""
import os

import pytest

from hil import SimTarget, battery_frames, decoded, make_target, read_trace, TRACES

@pytest.fixture
def target():
    return make_target()

def bodystate(frames):
    return [(t, d) for t, d in decoded(frames, "BodyStatus")]

def test_normal_battery_keeps_body_out_of_limp_home_after_startup(target):
    out = target.play(read_trace(TRACES / "battery_normal.log"), extra_s=0.3)
    late = [d for t, d in bodystate(out) if t > 2.0]
    assert late and all(d["LimpHome"] == 0 for d in late)
    hb = [d for _, d in decoded(out, "Heartbeat")]
    assert hb and all(d["NodeId"] == 2 and d["ProtoVer"] == 1 for d in hb)

def test_battery_fault_trace_sends_body_to_limp_home_within_deadline(target):
    out = target.play(read_trace(TRACES / "battery_fault_at_2s.log"), extra_s=1.0)
    limp = [t for t, d in bodystate(out) if d["LimpHome"] == 1 and t > 1.9]
    assert limp, "body never entered limp-home"
    latency = limp[0] - 2.0
    print(f"\n  [measured, sim 10 ms tick] fault frame at 2.00 s -> body limp-home status at {limp[0]:.2f} s")
    assert latency < 0.3
    d = [d for t, d in bodystate(out) if t == limp[0]][0]
    assert d["Lights"] == 4 and d["ServoDeg"] == 90      # fault lamp on, servo centred

def test_battery_going_silent_triggers_limp_home_after_peer_timeout(target):
    out = target.play(read_trace(TRACES / "battery_dies_at_1s.log"), extra_s=2.0)
    limp = [t for t, d in bodystate(out) if d["LimpHome"] == 1 and t > 1.0]
    assert limp, "silence did not trigger limp-home"
    assert 1.4 <= limp[0] <= 1.8      # last heartbeat at 0.9 s + 500 ms peer timeout (TARGET), plus one status period

def test_body_recovers_when_battery_returns(target):
    first = read_trace(TRACES / "battery_dies_at_1s.log")
    back = [f.__class__(f.t + 3.0, f.can_id, f.data) for f in battery_frames(4)]
    out = target.play(first + back, extra_s=0.5)
    assert any(d["LimpHome"] == 1 for _, d in bodystate(out))
    assert bodystate(out)[-1][1]["LimpHome"] == 0

def test_wrong_protocol_version_is_ignored(target):
    from fleetbus import canmsg
    from hil import Frame
    frames = []
    for k in range(40):
        cid, d = canmsg.encode("Heartbeat", NodeId=1, FwVersion=1, UptimeS=0, State=1, Slot=0, Trial=0, ProtoVer=9)
        frames.append(Frame(k / 10, cid, d))
    out = target.play(frames, extra_s=0.2)
    assert all(d["LimpHome"] == 1 for _, d in bodystate(out)), "a peer speaking another protocol version must not count as alive"

@pytest.mark.skipif(not os.environ.get("HIL_CAN"), reason="needs a real board")
def test_board_boots_and_heartbeats_within_a_second(target):
    out = target.play([], extra_s=1.2)
    assert decoded(out, "Heartbeat"), "no heartbeat from the board"
