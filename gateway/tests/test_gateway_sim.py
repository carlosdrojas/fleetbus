import pytest

pytest.importorskip("nacl")
from fleetbus import canmsg, fbp  # noqa: E402
from harness import World  # noqa: E402

pytest.importorskip("cantools")

def decoded(w, name):
    return [(t, d) for t, cid, data in w.telemetry() if (dm := canmsg.decode(cid, data)) and (d := dm[1]) is not None and dm[0] == name for t in [t]]

@pytest.fixture
def world():
    w = World(delay=10)
    yield w
    w.close()

def test_telemetry_reaches_backend_in_order_with_real_firmware_frames(world):
    world.run(5000)
    hb = decoded(world, "Heartbeat")
    assert {d["NodeId"] for _, d in hb} == {1, 2}
    assert all(d["ProtoVer"] == 1 and d["FwVersion"] == 1 for _, d in hb)
    battery = decoded(world, "BatteryStatus")
    assert battery and all(d["PackMilliVolt"] == 3800 for _, d in battery)
    assert [p.seq for p in world.be.delivered] == list(range(1, len(world.be.delivered) + 1))

def test_gateway_heartbeat_reports_stats(world):
    world.run(3500)
    hbs = [fbp.dec_heartbeat(p.payload) for p in world.be.delivered if p.type == fbp.HEARTBEAT]
    assert len(hbs) >= 3 and hbs[-1][0] >= 2          # uptime advances

def test_injected_overtemp_shows_in_telemetry_and_puts_body_in_limp_home(world):
    world.run(3000)
    assert not world.fleet.nodes[2].limp_home
    world.fleet.nodes[1].sensors["dc"] = 700
    world.run(1500)
    assert world.fleet.nodes[2].limp_home
    faults = decoded(world, "FaultEvent")
    assert faults and faults[0][1]["FaultCode"] == 0x10
    body = decoded(world, "BodyStatus")
    assert body[-1][1]["LimpHome"] == 1

def test_unplugging_battery_is_visible_as_silence(world):
    world.run(3000)
    world.fleet.nodes[1].powered = False
    t_cut = world.now
    world.run(2000)
    last_bat = max(t for t, d in decoded(world, "Heartbeat") if d["NodeId"] == 1)
    assert t_cut - 200 <= last_bat <= t_cut + 20
    assert world.fleet.nodes[2].limp_home

def test_ota_frames_are_not_forwarded_as_telemetry(world):
    world.run(1000)
    ids = {cid for _, cid, _ in world.telemetry()}
    assert not (ids & canmsg.OTA_IDS)

def test_gateway_survives_garbage_datagrams(world):
    world.run(500)
    world.down.send(b"\x00" * 30)
    world.down.send(b"")
    world.down.send(bytes(range(200)))
    world.run(500)
    assert world.gw.bad_datagrams >= 2
