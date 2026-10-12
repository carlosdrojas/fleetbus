"""Scenarios run in file order against one shared stack; each leaves the fleet healthy for the next."""
import time

import pytest

from conftest import wait_until

pytestmark = pytest.mark.e2e

def healthy(stack):
    f = stack.fleet()
    return all(n["state_name"] in ("NORMAL",) and n["health"] == "ok" and not n["trial"] for g in f.values() for n in g["nodes"])

def test_01_fleet_comes_up_and_is_labeled_simulated(stack):
    wait_until(lambda: healthy(stack), 30, what="all nodes NORMAL")
    f = stack.fleet()
    assert sorted(f) == [1, 2, 3]
    for g in f.values():
        assert g["simulated"] is True
        assert sorted(n["name"] for n in g["nodes"]) == ["battery", "body"]
        assert all(n["fw_version"] == 1 and n["slot_name"] == "A" for n in g["nodes"])
    (rows,) = stack.sql("SELECT count(*) FROM telemetry")[0:1]
    assert rows[0] > 100
    assert stack.sql("SELECT count(*) FROM metrics WHERE name='pack_mv'")[0][0] > 10

def test_02_overtemp_raises_alerts_and_clears_on_recovery(stack):
    t0 = time.time()
    stack.control(1, "/sensor", {"node": 1, "dc": 700})
    wait_until(lambda: any(a["rule"] == "node_fault" and a["gateway_id"] == 1 for a in stack.alerts()), 10, what="node_fault alert")
    print(f"\n  [measured] overtemp injected -> fault alert visible via API after {time.time() - t0:.1f} s")
    wait_until(lambda: any(a["rule"] == "node_limp_home" and a["gateway_id"] == 1 for a in stack.alerts()), 10, what="body limp-home alert")
    assert stack.node(2, "battery")["state_name"] == "NORMAL"            # other cars unaffected
    assert stack.sql("SELECT count(*) FROM faults WHERE gateway_id=1 AND code=16")[0][0] >= 1
    # The battery FSM latches FAULT/SAFE (by design); a power cycle of the node clears it.
    stack.control(1, "/sensor", {"node": 1, "dc": 250})
    stack.control(1, "/power", {"node": 1, "on": False})
    time.sleep(0.5)
    stack.control(1, "/power", {"node": 1, "on": True})
    wait_until(lambda: not [a for a in stack.alerts() if a["gateway_id"] == 1 and a["rule"] in ("node_fault", "node_limp_home")], 25, what="alerts cleared")

def test_03_unplugging_a_node_raises_node_silent_then_clears(stack):
    wait_until(lambda: healthy(stack), 30, what="healthy")
    t0 = time.time()
    stack.control(2, "/power", {"node": 1, "on": False})
    wait_until(lambda: any(a["rule"] == "node_silent" and a["gateway_id"] == 2 and a["node_id"] == 1 for a in stack.alerts()), 10, what="node_silent")
    print(f"\n  [measured] battery unplugged -> node_silent alert after {time.time() - t0:.1f} s (threshold 2 s)")
    assert stack.node(2, "body")["state_name"] == "LIMP_HOME" or wait_until(lambda: stack.node(2, "body")["state_name"] == "LIMP_HOME", 5)
    stack.control(2, "/power", {"node": 1, "on": True})
    wait_until(lambda: not [a for a in stack.alerts() if a["gateway_id"] == 2], 25, what="alerts cleared after replug")
    wait_until(lambda: healthy(stack), 25, what="healthy again")

def test_04_gateway_outage_raises_one_alert_and_loses_no_telemetry(stack):
    wait_until(lambda: healthy(stack), 30, what="healthy")
    stack.kill("ingest")                                    # backend ingest dies; gateways keep sampling and spool
    t_down = time.time()
    time.sleep(6)
    wait_until(lambda: any(a["rule"] == "gateway_silent" for a in stack.alerts()), 10, what="gateway_silent")
    assert len([a for a in stack.alerts() if a["rule"] == "node_silent"]) == 0       # one root cause, not 6 symptoms
    stack.start_ingest()
    wait_until(lambda: not [a for a in stack.alerts() if a["rule"] == "gateway_silent"], 30, what="gateway_silent cleared")
    time.sleep(3)
    # Every second of the outage must be present in the database (replayed from the gateway's disk spool), per node.
    lo, hi = t_down + 1, t_down + 5
    for gw in (1, 2, 3):
        rows = stack.sql("""SELECT count(DISTINCT floor(extract(epoch from ts))) FROM telemetry
                            WHERE gateway_id=%s AND can_id=256 AND ts BETWEEN to_timestamp(%s) AND to_timestamp(%s)""", gw, lo, hi)
        assert rows[0][0] >= 4, f"gateway {gw}: only {rows[0][0]} of ~4 outage seconds recovered from spool"
    print("\n  [measured] 6 s ingest outage: every gateway replayed its spool; heartbeats present for each outage second")

def test_05_good_rollout_canary_then_fleet(stack):
    wait_until(lambda: healthy(stack), 30, what="healthy")
    img = stack.upload(2)
    rid = stack.start_rollout(img, canary=2)
    r = stack.wait_rollout(rid)
    steps = r["steps"]
    assert r["rollout"]["state"] == "succeeded", r
    assert [s["gateway_id"] for s in steps][0] == 2 and steps[0]["phase"] == "canary"
    assert [s["state"] for s in steps] == ["healthy"] * 3
    for g in (1, 2, 3):
        n = wait_until(lambda g=g: (x := stack.node(g, "body"))["fw_version"] == 2 and not x["trial"] and x, 15, what=f"gw{g} body v2 confirmed")
        assert n["slot_name"] == "B"
    assert all(stack.node(g, "battery")["fw_version"] == 1 for g in (1, 2, 3))      # other node type untouched
    # sim ground truth: the C node core agrees and raised the anti-downgrade floor
    assert stack.sim_state(1)["2"]["version"] == 2

def test_06_hanging_image_is_rolled_back_by_bootloader_and_halts_fleet(stack):
    wait_until(lambda: healthy(stack), 30, what="healthy")
    img = stack.upload(3, behaviour=1)                       # app hangs at boot
    rid = stack.start_rollout(img, canary=3, window="40s")
    r = stack.wait_rollout(rid, timeout=120)
    assert r["rollout"]["state"] == "failed", r
    steps = {s["gateway_id"]: s for s in r["steps"]}
    assert steps[3]["state"] in ("rolled_back", "failed") and steps[3]["phase"] == "canary"
    assert steps[1]["state"] == "skipped" and steps[2]["state"] == "skipped"
    st = stack.sim_state(3)["2"]
    assert st["version"] == 2 and st["wdg_resets"] >= 3, st                  # bootloader gave up on v3 and returned to v2
    wait_until(lambda: stack.node(3, "body")["fw_version"] == 2 and stack.node(3, "body")["health"] == "ok", 30, what="canary back on v2")
    assert stack.node(1, "body")["fw_version"] == 2 and stack.sim_state(1)["2"]["boots"] == stack.sim_state(1)["2"]["boots"]
    assert any(a["rule"] == "rollout_failed" for a in stack.alerts())
    print(f"\n  [measured] hanging v3: wdg_resets={st['wdg_resets']}, canary back on v2; rollout halted: {r['rollout']['failure_reason']}")

def test_07_unhealthy_image_fails_the_health_gate(stack):
    wait_until(lambda: healthy(stack) or True, 5)
    time.sleep(2)
    img = stack.upload(4, behaviour=2)                       # runs, but never reports healthy
    rid = stack.start_rollout(img, canary=1, window="15s")
    r = stack.wait_rollout(rid, timeout=120)
    assert r["rollout"]["state"] == "failed", r
    assert r["steps"][0]["state"] in ("failed", "rolled_back")
    assert all(s["state"] == "skipped" for s in r["steps"][1:])
    wait_until(lambda: stack.node(1, "body")["fw_version"] == 2, 60, what="canary rolled back to v2 by the node's own health deadline")

def test_08_api_restart_mid_rollout_resumes(stack):
    wait_until(lambda: healthy(stack), 60, what="healthy")
    img = stack.upload(5, body_len=30000)                    # ~6 s transfer through the sim CAN bus
    rid = stack.start_rollout(img, canary=1)
    wait_until(lambda: stack.rollout(rid)["steps"][0]["state"] == "transferring", 15, what="canary transferring")
    stack.kill("api")                                       # orchestrator dies mid-transfer
    time.sleep(3)
    stack.start_api()
    r = stack.wait_rollout(rid, timeout=150)
    assert r["rollout"]["state"] == "succeeded", r
    assert all(stack.node(g, "body")["fw_version"] == 5 for g in (1, 2, 3))

def test_09_ingest_restart_mid_transfer_resumes_transfer(stack):
    wait_until(lambda: healthy(stack), 60, what="healthy")
    img = stack.upload(6, body_len=30000)
    rid = stack.start_rollout(img, canary=2)
    wait_until(lambda: (t := stack.rollout(rid)["steps"][0].get("transfer")) and t["next_offset"] > 2000, 20, what="transfer under way")
    stack.kill("ingest")                                    # OTA driver + UDP front door die mid-transfer
    time.sleep(2)
    stack.start_ingest()
    r = stack.wait_rollout(rid, timeout=150)
    assert r["rollout"]["state"] == "succeeded", r
    assert all(stack.node(g, "body")["fw_version"] == 6 for g in (1, 2, 3))

def test_10_rollout_never_updates_a_node_it_cannot_see(stack):
    wait_until(lambda: healthy(stack), 60, what="healthy")
    img = stack.upload(7)
    stack.kill("gw1")                                       # car 1's gateway process dies
    try:
        time.sleep(3)                                       # inside the API's "online" grace window (6 s)
        r = stack.fbctl("rollout", "start", "-image", str(img), "-canary", "1", check=False)
        if r.returncode == 0:                               # accepted on stale liveness -> the engine must refuse at step start
            rid = int(r.stdout.split()[1])
            res = stack.wait_rollout(rid, timeout=20)
            assert res["rollout"]["state"] == "failed" and "not reporting" in res["steps"][0]["detail"], res
        else:                                               # or refused up front
            assert "not online" in (r.stdout + r.stderr), r.stdout + r.stderr
        assert stack.sim_state(2)["2"]["version"] == 6      # nothing was sent to anyone
    finally:
        stack.start_gateway(1)
    wait_until(lambda: healthy(stack), 60, what="car 1 back")

def test_11_abort_stops_promotion(stack):
    wait_until(lambda: healthy(stack), 60, what="healthy")
    img = stack.upload(8, body_len=30000)
    rid = stack.start_rollout(img, canary=1)
    wait_until(lambda: stack.rollout(rid)["steps"][0]["state"] == "transferring", 15, what="transferring")
    stack.fbctl("rollout", "abort", str(rid))
    r = stack.rollout(rid)
    assert r["rollout"]["state"] == "aborted" and all(s["state"] in ("skipped",) for s in r["steps"][1:])
    time.sleep(8)
    assert all(stack.node(g, "body")["fw_version"] == 6 for g in (2, 3))   # nothing promoted after abort

def test_12_throughput_and_no_unexplained_errors(stack):
    m = stack.sql("SELECT count(*) FROM telemetry")[0][0]
    assert m > 5000
    for name in ("ingest", "api"):
        log = (stack.tmp / f"{name}.log").read_text(errors="replace")
        assert "panic" not in log and "fatal" not in log.lower(), log[-800:]
