"""OTA from the (reference) backend through FBP/UDP, the gateway and a simulated CAN bus into the real firmware node core."""
import pytest

from fleetbus import canmsg, fbp, image
from harness import KEY, OtaServer, World

def make_image(node, version, slot, behaviour=0, size=3000, key=KEY):
    return image.build_image(bytes([behaviour, version & 0xFF]) + bytes((i * 13) & 0xFF for i in range(size)), node, slot, version, key)

def start_ota(w, node_id, blob, version):
    srv = OtaServer(w.be, node_id, blob, slot=1, version=version)
    srv.start(w.now)
    return srv

def run_until(w, cond, limit_ms, step=50):
    end = w.now + limit_ms
    while w.now < end and not cond():
        w.run(step)
    return cond()

@pytest.fixture
def world():
    w = World(delay=10)
    w.run(3000)                          # let the fleet settle (body leaves its start-up limp-home)
    yield w
    w.close()

def test_good_update_installs_confirms_and_reports_new_version(world):
    body = world.fleet.nodes[2]
    srv = start_ota(world, 2, make_image(2, 2, 1), 2)
    assert run_until(world, lambda: srv.final, 20_000)
    assert srv.final[0] == fbp.OTA_TRANSFERRED
    world.run(3000)
    assert (body.version, body.slot, body.slot_status(1), body.min_version) == (2, 1, "CONFIRMED", 2)
    hb = [canmsg.decode(c, d)[1] for _, c, d in world.telemetry() if canmsg.decode(c, d) and canmsg.decode(c, d)[0] == "Heartbeat"]
    assert [h["FwVersion"] for h in hb if h["NodeId"] == 2][-1] == 2
    assert not body.limp_home

def test_other_node_keeps_running_during_ota(world):
    bat = world.fleet.nodes[1]
    srv = start_ota(world, 2, make_image(2, 2, 1), 2)
    run_until(world, lambda: srv.final, 20_000)
    assert bat.state_name == "NORMAL" and bat.boots == 1

def test_update_survives_30pct_udp_loss():
    w = World(loss=0.3, dup=0.05, reorder=0.1, delay=20)
    try:
        w.run(4000)
        srv = start_ota(w, 2, make_image(2, 2, 1), 2)
        assert run_until(w, lambda: srv.final, 60_000)
        assert srv.final[0] == fbp.OTA_TRANSFERRED
        w.run(3000)
        assert w.fleet.nodes[2].slot_status(1) == "CONFIRMED"
    finally:
        w.close()

def test_update_survives_10pct_can_frame_loss_both_ways():
    w = World(delay=10, can_drop_to=0.1, can_drop_from=0.1)
    try:
        w.run(4000)
        srv = start_ota(w, 2, make_image(2, 2, 1), 2)
        assert run_until(w, lambda: srv.final, 90_000)
        assert srv.final[0] == fbp.OTA_TRANSFERRED
        assert w.can.dropped > 0
    finally:
        w.close()

def test_hanging_image_is_rolled_back(world):
    body = world.fleet.nodes[2]
    srv = start_ota(world, 2, make_image(2, 2, 1, behaviour=1), 2)
    assert run_until(world, lambda: srv.final, 20_000)
    world.run(15_000)
    assert (body.version, body.slot, body.slot_status(1), body.slot_status(0)) == (1, 0, "BAD", "CONFIRMED")
    assert body.alive and body.wdg_resets == 3

def test_unhealthy_image_is_rolled_back_after_health_deadline(world):
    body = world.fleet.nodes[2]
    srv = start_ota(world, 2, make_image(2, 2, 1, behaviour=2), 2)
    run_until(world, lambda: srv.final, 20_000)
    world.run(60_000)
    assert (body.version, body.slot_status(1)) == (1, "BAD")

def test_tampered_image_is_rejected_by_the_node(world):
    blob = bytearray(make_image(2, 2, 1))
    blob[1200] ^= 0x01
    body = world.fleet.nodes[2]
    srv = start_ota(world, 2, bytes(blob), 2)
    assert run_until(world, lambda: srv.final, 20_000)
    assert srv.final[0] == fbp.OTA_VERIFY_FAILED
    assert body.version == 1 and body.slot_status(1) == "EMPTY" and body.boots == 1

def test_image_signed_with_the_wrong_key_is_rejected(world):
    from nacl.signing import SigningKey
    srv = start_ota(world, 2, make_image(2, 2, 1, key=SigningKey(bytes([9] * 32))), 2)
    run_until(world, lambda: srv.final, 20_000)
    assert srv.final[0] == fbp.OTA_VERIFY_FAILED

def test_downgrade_is_rejected(world):
    srv = start_ota(world, 2, make_image(2, 1, 1), 1)         # same version as running
    run_until(world, lambda: srv.final, 20_000)
    assert srv.final[0] == fbp.OTA_VERIFY_FAILED

def test_image_for_wrong_node_is_rejected(world):
    srv = start_ota(world, 2, make_image(1, 5, 1), 5)         # battery image offered to the body node
    run_until(world, lambda: srv.final, 20_000)
    assert srv.final[0] == fbp.OTA_VERIFY_FAILED

def test_update_to_unreachable_node_fails_cleanly(world):
    world.fleet.nodes[2].powered = False
    srv = start_ota(world, 2, make_image(2, 2, 1), 2)
    run_until(world, lambda: srv.final, 30_000)
    assert srv.final and srv.final[0] == fbp.OTA_FAILED
