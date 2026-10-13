"""FleetBus Lite live dashboard (gateway/fleetbus/lite_dash.py): decoding, node liveness, the simulator source
running the real firmware logic, and the HTTP/SSE endpoints."""
import json
import sys
import threading
import urllib.request
from pathlib import Path

import cantools
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gateway"))
from fleetbus import lite_dash  # noqa: E402

DB = cantools.database.load_file(str(ROOT / "can" / "fleetbus.dbc"))

def frame(name, **sig):
    m = DB.get_message_by_name(name)
    full = {s.name: 0 for s in m.signals}
    full["ProtoVer"] = 1
    full.update(sig)
    return m.frame_id, m.encode(full)

def test_decodes_battery_body_and_heartbeats():
    st = lite_dash.LiteState()
    st.feed(1000, *frame("BatteryStatus", PackMilliVolt=3712, CurrentMilliAmp=-842, TempDeciC=251, SocPct=59))
    st.feed(1005, *frame("BodyStatus", Lights=3, ServoDeg=97, LimpHome=0))
    st.feed(1010, *frame("Heartbeat", NodeId=1, FwVersion=2, UptimeS=40, State=2))
    st.feed(1020, *frame("Heartbeat", NodeId=2, FwVersion=2, UptimeS=39, State=1))
    st.feed(1030, 0x7F0, b"\x01\x02")                      # not in the DBC (e.g. the CAN self-test id)
    s = st.snapshot(1100)
    assert s["battery"] == {"pack_v": 3.712, "current_a": -0.842, "temp_c": 25.1, "soc_pct": 59, "age_ms": 100}
    assert s["body"]["servo_deg"] == 97 and s["body"]["limp_home"] is False
    assert [(n["name"], n["state"], n["alive"]) for n in s["nodes"]] == [("battery", "WARNING", True), ("body", "NORMAL", True)]
    assert s["bus"]["frames"] == 5 and s["bus"]["unknown"] == 1

def test_node_goes_silent_after_the_firmware_peer_timeout():
    st = lite_dash.LiteState()
    st.feed(0, *frame("Heartbeat", NodeId=1, State=1))
    assert st.snapshot(lite_dash.ALIVE_MS - 1)["nodes"][0]["alive"] is True
    assert st.snapshot(lite_dash.ALIVE_MS + 1)["nodes"][0]["alive"] is False

def test_sim_source_runs_the_real_firmware_logic():
    pytest.importorskip("ctypes")
    try:
        src = lite_dash.SimSource()
    except FileNotFoundError as e:
        pytest.skip(str(e))
    st = lite_dash.LiteState()
    t = src.base
    def run(ms):
        nonlocal t
        for _ in range(ms // 50):
            t += 50
            for f in src.poll(t):
                st.feed(*f)
    run(4000)                                               # body boots in limp-home (fail-safe) until the battery has
    s = st.snapshot(t)                                      # been healthy for its recovery time
    assert s["battery"]["pack_v"] == 3.8 and {n["state"] for n in s["nodes"]} == {"NORMAL"}
    src.control({"pack_mv": 2900})                          # turn the "knob" below the fault threshold
    run(1500)
    s = st.snapshot(t)
    states = {n["name"]: n["state"] for n in s["nodes"]}
    assert states["battery"] in ("FAULT", "SAFE") and states["body"] == "LIMP_HOME" and s["body"]["limp_home"] is True
    src.control({"battery_power": False})                   # "unplug" the battery node
    run(1000)
    assert {n["name"]: n["alive"] for n in st.snapshot(t)["nodes"]}["battery"] is False
    src.fleet.close()

def test_http_state_page_and_sim_control():
    try:
        src = lite_dash.SimSource()
    except FileNotFoundError as e:
        pytest.skip(str(e))
    server, state, stop = lite_dash.serve(src, "sim", port=0)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_port}"
    try:
        assert "FleetBus Lite" in urllib.request.urlopen(base + "/").read().decode()
        s = json.loads(urllib.request.urlopen(base + "/api/state").read())
        assert s["mode"] == "sim" and s["sim"]["pack_mv"] == 3800
        req = urllib.request.Request(base + "/api/sim", data=json.dumps({"pack_mv": 3100}).encode(), method="POST")
        assert json.loads(urllib.request.urlopen(req).read())["pack_mv"] == 3100
        with urllib.request.urlopen(base + "/events", timeout=5) as r:
            line = r.readline().decode()
        assert line.startswith("data: ") and json.loads(line[6:])["mode"] == "sim"
    finally:
        stop.set(); server.shutdown(); src.fleet.close()

def test_controls_refused_on_a_real_bus():
    class FakeCan(lite_dash.CanSource):
        def __init__(self):                                 # no adapter needed
            self.t0 = 0
    with pytest.raises(PermissionError):
        FakeCan().control({"pack_mv": 3000})
