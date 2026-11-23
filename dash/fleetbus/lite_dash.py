"""FleetBus Lite live dashboard.

Reads the CAN bus, decodes every frame with can/fleetbus.dbc, and serves a single web page with live gauges, node
states and heartbeat ages over Server-Sent Events. No database, no backend, no internet needed (no external fonts or
scripts), so it runs on a laptop at a career fair.

  python -m fleetbus.lite_dash --can slcan:/dev/tty.usbmodem1      real bus through the USB-CAN adapter (CANable, slcan)
  python -m fleetbus.lite_dash --sim                               the simulator running the real firmware logic;
                                                                   the page gets a "pack voltage" slider and an
                                                                   "unplug battery" switch
Then open http://127.0.0.1:8000. Read-only on a real bus: it never transmits.
"""
from __future__ import annotations

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cantools

ROOT = Path(__file__).resolve().parents[2]
DBC = ROOT / "can" / "fleetbus.dbc"
ALIVE_MS = 500                       # kPeerTimeoutMs in firmware/common/node.hpp
NODE_NAMES = {1: "battery", 2: "body"}


class LiteState:
    """Latest decoded value of every message, per-node heartbeat bookkeeping and counters. Thread-safe."""

    def __init__(self, dbc_path: Path = DBC):
        self.db = cantools.database.load_file(str(dbc_path))
        self.lock = threading.Lock()
        self.msgs: dict[str, dict] = {}
        self.nodes: dict[int, dict] = {}
        self.total = 0
        self.unknown = 0
        self.decode_errors = 0
        self.recent: list[dict] = []
        self.started_ms: float | None = None

    def feed(self, t_ms: float, can_id: int, data: bytes) -> None:
        with self.lock:
            self.total += 1
            if self.started_ms is None:
                self.started_ms = t_ms
            try:
                hb = self.db.get_message_by_name("Heartbeat").frame_id   # sent at 0x100 + node id
                msg = self.db.get_message_by_frame_id(hb if can_id & ~0xF == hb else can_id)
            except KeyError:
                self.unknown += 1
                return
            try:
                sig = msg.decode(data, decode_choices=True)
            except Exception:                               # malformed frame: count it, keep going
                self.decode_errors += 1
                return
            sig = {k: (str(v) if not isinstance(v, (int, float)) else v) for k, v in sig.items()}
            self.msgs[msg.name] = {"t": t_ms, "signals": sig, "id": can_id}
            if msg.name == "Heartbeat":
                nid = int(sig["NodeId"])
                self.nodes[nid] = {"t": t_ms, "state": sig["State"], "fw": sig["FwVersion"], "uptime_s": sig["UptimeS"]}
            self.recent.append({"t": t_ms, "id": can_id, "name": msg.name, "signals": sig})
            del self.recent[:-12]

    def snapshot(self, now_ms: float) -> dict:
        with self.lock:
            def age(t):
                return None if t is None else max(0, round(now_ms - t))
            b = self.msgs.get("BatteryStatus")
            body = self.msgs.get("BodyStatus")
            nodes = []
            for nid in sorted(self.nodes):
                n = self.nodes[nid]
                a = age(n["t"])
                nodes.append({"id": nid, "name": NODE_NAMES.get(nid, f"node {nid}"), "state": n["state"], "fw": n["fw"],
                              "uptime_s": n["uptime_s"], "heartbeat_age_ms": a, "alive": a is not None and a < ALIVE_MS})
            elapsed = (now_ms - self.started_ms) / 1000 if self.started_ms is not None else 0
            return {
                "battery": None if b is None else {
                    "pack_v": b["signals"]["PackMilliVolt"] / 1000, "current_a": b["signals"]["CurrentMilliAmp"] / 1000,
                    "temp_c": b["signals"]["TempDeciC"] / 10, "soc_pct": b["signals"]["SocPct"], "age_ms": age(b["t"])},
                "body": None if body is None else {
                    "lights": body["signals"]["Lights"], "servo_deg": body["signals"]["ServoDeg"],
                    "limp_home": bool(body["signals"]["LimpHome"]), "age_ms": age(body["t"])},
                "nodes": nodes,
                "bus": {"frames": self.total, "unknown": self.unknown, "decode_errors": self.decode_errors,
                        "rate_fps": round(self.total / elapsed, 1) if elapsed > 0 else 0.0},
                "recent": [dict(r, age_ms=age(r["t"])) for r in reversed(self.recent)],
            }


class SimSource:
    """The host simulator (real firmware node core) as the bus. Advances in real time unless stepped explicitly."""

    def __init__(self):
        from .fbsim import SimFleet
        self.fleet = SimFleet()
        self.lock = threading.Lock()                    # the HTTP thread changes sensors while the pump thread steps
        self.t0 = time.monotonic()
        self.base = self.fleet.now

    def now_ms(self) -> float:
        return self.base + (time.monotonic() - self.t0) * 1000

    def poll(self, now_ms: float | None = None):
        with self.lock:
            return self.fleet.step(int(self.now_ms() if now_ms is None else now_ms))

    def control(self, req: dict) -> dict:
        with self.lock:
            bat = self.fleet.nodes[1]
            if "pack_mv" in req:
                bat.sensors["mv"] = max(2500, min(4300, int(req["pack_mv"])))
            if "battery_power" in req:
                self.fleet.set_power(1, bool(req["battery_power"]))
            return {"pack_mv": bat.sensors["mv"], "battery_power": bat.powered}


class CanSource:
    """A real bus through python-can (e.g. slcan:/dev/tty.usbmodem1). Receive only."""

    def __init__(self, spec: str, bitrate: int = 500000):
        import can
        interface, channel = spec.split(":", 1)
        self.bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate)
        self.t0 = time.monotonic()

    def now_ms(self) -> float:
        return (time.monotonic() - self.t0) * 1000

    def poll(self, now_ms: float | None = None):
        out = []
        while True:
            m = self.bus.recv(timeout=0.02)
            if m is None:
                return out
            if not m.is_error_frame and not m.is_remote_frame:
                out.append((self.now_ms(), m.arbitration_id, bytes(m.data)))

    def control(self, req: dict) -> dict:
        raise PermissionError("controls exist only in --sim mode; on the bench, turn the real knob")


def pump(state: LiteState, source, stop: threading.Event) -> None:
    while not stop.is_set():
        for t, cid, data in source.poll():
            state.feed(t, cid, data)
        if isinstance(source, SimSource):
            time.sleep(0.02)


def make_handler(state: LiteState, source, mode: str):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *a):                          # quiet console
            pass

        def _send(self, code: int, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _snap(self) -> dict:
            s = state.snapshot(source.now_ms())
            s["mode"] = mode
            if mode == "sim":
                s["sim"] = source.control({})
            return s

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                self._send(200, PAGE.encode(), "text/html; charset=utf-8")
            elif self.path == "/api/state":
                self._send(200, json.dumps(self._snap()).encode(), "application/json")
            elif self.path == "/events":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                try:
                    while True:
                        self.wfile.write(b"data: " + json.dumps(self._snap()).encode() + b"\n\n")
                        self.wfile.flush()
                        time.sleep(0.2)
                except (BrokenPipeError, ConnectionResetError):
                    return
            else:
                self._send(404, b"not found", "text/plain")

        def do_POST(self):
            if self.path != "/api/sim":
                return self._send(404, b"not found", "text/plain")
            try:
                req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0)) or 0) or b"{}")
                self._send(200, json.dumps(source.control(req)).encode(), "application/json")
            except PermissionError as e:
                self._send(403, str(e).encode(), "text/plain")
            except (ValueError, TypeError) as e:
                self._send(400, str(e).encode(), "text/plain")

    return Handler


def serve(source, mode: str, host: str = "127.0.0.1", port: int = 8000):
    """Start the pump thread and the HTTP server. Returns (server, state, stop_event); call server.serve_forever()."""
    state = LiteState()
    stop = threading.Event()
    threading.Thread(target=pump, args=(state, source, stop), daemon=True).start()
    server = ThreadingHTTPServer((host, port), make_handler(state, source, mode))
    server.daemon_threads = True
    return server, state, stop


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--sim", action="store_true", help="simulated battery + body nodes (real firmware logic)")
    src.add_argument("--can", metavar="INTERFACE:CHANNEL", help="python-can adapter, e.g. slcan:/dev/tty.usbmodem1")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    a = ap.parse_args()
    source = SimSource() if a.sim else CanSource(a.can, a.bitrate)
    server, _, stop = serve(source, "sim" if a.sim else "can", a.host, a.port)
    print(f"FleetBus Lite dashboard ({'simulator' if a.sim else a.can}) on http://{a.host}:{server.server_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
    return 0


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FleetBus Lite</title>
<style>
/* Instrument panel: gauges across the top, node cards, then the raw bus. System fonts only (works offline). */
:root{--bg:#eef1f0;--panel:#fff;--ink:#121a1c;--muted:#56646a;--line:#d0d8d8;--volt:#17708f;--temp:#a95f00;--amp:#2c7a47;
 --ok:#2c7a47;--okbg:#dcefe2;--warn:#a96d00;--warnbg:#f6e7c8;--bad:#b2362c;--badbg:#f6d9d6;--track:#dfe5e6;
 --mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace;--sans:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
@media (prefers-color-scheme:dark){:root{--bg:#0d1214;--panel:#151c1f;--ink:#e3eaec;--muted:#93a3a8;--line:#28343a;--volt:#5cb6d5;
 --temp:#eea63c;--amp:#5cc083;--ok:#5cc083;--okbg:#1f3d2b;--warn:#e7b04a;--warnbg:#4a3a14;--bad:#ee6a60;--badbg:#4a1d1a;--track:#26323a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 var(--sans)}
main{max-width:1100px;margin:0 auto;padding:20px 16px 40px;display:grid;gap:16px}
main>*{min-width:0}td:last-child{word-break:break-word}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:8px 16px}h1{margin:0;font-size:26px;letter-spacing:-.01em}
.sub{color:var(--muted);font:12px var(--mono)}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--muted);margin-right:6px}
.dot.on{background:var(--ok)}.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:16px}
.gauges{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px}
.g{display:grid;justify-items:center}.g svg{width:100%;max-width:220px;height:auto}.g .v{font:600 30px var(--sans);font-variant-numeric:tabular-nums}
.g .l{color:var(--muted);font-size:13px;margin-top:-6px}
.nodes{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px}
.node h2{margin:0 0 8px;font-size:18px;text-transform:capitalize}.pill{display:inline-block;font:600 12px var(--sans);padding:4px 10px;border-radius:999px}
.st-NORMAL{background:var(--okbg);color:var(--ok)}.st-WARNING,.st-LIMP{background:var(--warnbg);color:var(--warn)}
.st-FAULT,.st-SAFE,.st-SILENT,.st-LIMP_HOME{background:var(--badbg);color:var(--bad)}.st-other{background:var(--track);color:var(--muted)}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:10px 0 0;font-size:14px}dt{color:var(--muted)}dd{margin:0;font-variant-numeric:tabular-nums}
.sim{display:flex;flex-wrap:wrap;gap:12px 20px;align-items:center}.sim[hidden]{display:none}.sim input[type=range]{width:min(320px,70vw)}
table{width:100%;border-collapse:collapse;font:12px var(--mono)}th,td{text-align:left;padding:5px 8px;border-bottom:1px solid var(--line);vertical-align:top}
th{color:var(--muted);font-weight:500}.wrap{overflow-x:auto}.empty{color:var(--muted)}
</style></head><body><main>
<header><h1>FleetBus Lite</h1><span class="sub" id="src"><span class="dot" id="live"></span>connecting…</span></header>
<section class="panel gauges" aria-label="Battery gauges">
 <div class="g"><svg viewBox="0 0 200 130" role="img" aria-label="Pack voltage"><path d="M20 110 A80 80 0 0 1 180 110" fill="none" stroke="var(--track)" stroke-width="16" stroke-linecap="round"/><path id="g-v" d="M20 110 A80 80 0 0 1 180 110" fill="none" stroke="var(--volt)" stroke-width="16" stroke-linecap="round" pathLength="100" stroke-dasharray="0 100"/></svg><div class="v" id="t-v">–</div><div class="l">pack voltage (V) · warn &lt; 3.30 · fault &lt; 3.00</div></div>
 <div class="g"><svg viewBox="0 0 200 130" role="img" aria-label="Temperature"><path d="M20 110 A80 80 0 0 1 180 110" fill="none" stroke="var(--track)" stroke-width="16" stroke-linecap="round"/><path id="g-t" d="M20 110 A80 80 0 0 1 180 110" fill="none" stroke="var(--temp)" stroke-width="16" stroke-linecap="round" pathLength="100" stroke-dasharray="0 100"/></svg><div class="v" id="t-t">–</div><div class="l">temperature (°C) · warn ≥ 45 · fault ≥ 60</div></div>
 <div class="g"><svg viewBox="0 0 200 130" role="img" aria-label="Current"><path d="M20 110 A80 80 0 0 1 180 110" fill="none" stroke="var(--track)" stroke-width="16" stroke-linecap="round"/><path id="g-a" d="M20 110 A80 80 0 0 1 180 110" fill="none" stroke="var(--amp)" stroke-width="16" stroke-linecap="round" pathLength="100" stroke-dasharray="0 100"/></svg><div class="v" id="t-a">–</div><div class="l">current (A)</div></div>
</section>
<section class="nodes" id="nodes"><div class="panel empty">Waiting for heartbeats…</div></section>
<section class="panel sim" id="sim" hidden aria-label="Simulator controls">
 <strong>Simulator controls</strong>
 <label>pack voltage <input type="range" id="mv" min="2500" max="4300" step="10" value="3800"> <span id="mv-v">3.80 V</span></label>
 <label><input type="checkbox" id="pwr" checked> battery node powered</label>
</section>
<section class="panel"><div class="wrap"><table><thead><tr><th>age</th><th>id</th><th>message</th><th>signals</th></tr></thead><tbody id="frames"><tr><td colspan="4" class="empty">No frames yet.</td></tr></tbody></table></div>
<div class="sub" id="busline" style="margin-top:8px"></div></section>
</main>
<script>
var $=function(id){return document.getElementById(id)};
function esc(s){return String(s).replace(/[&<>"]/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function arc(id,v,lo,hi){var f=Math.max(0,Math.min(1,(v-lo)/(hi-lo)));$(id).setAttribute('stroke-dasharray',(f*100).toFixed(1)+' 100')}
function cls(st){return ['NORMAL','WARNING','FAULT','SAFE','LIMP_HOME','SILENT'].indexOf(st)>=0?'st-'+st:'st-other'}
function render(s){
 $('live').classList.add('on');$('src').lastChild.textContent=(s.mode==='sim'?'simulator (real firmware logic)':'live CAN bus')+' · 500 kbit/s';
 var b=s.battery;
 if(b){arc('g-v',b.pack_v,3.0,4.2);$('t-v').textContent=b.pack_v.toFixed(2);arc('g-t',b.temp_c,0,60);$('t-t').textContent=b.temp_c.toFixed(1);
  arc('g-a',Math.abs(b.current_a),0,2);$('t-a').textContent=b.current_a.toFixed(2)}
 var html=s.nodes.map(function(n){var st=n.alive?n.state:'SILENT';var extra='';
   if(n.name==='battery'&&b)extra='<dt>state of charge</dt><dd>'+b.soc_pct+' %</dd>';
   if(n.name==='body'&&s.body)extra='<dt>limp-home</dt><dd>'+(s.body.limp_home?'yes':'no')+'</dd><dt>servo</dt><dd>'+s.body.servo_deg+'°</dd><dt>lights</dt><dd>0x'+s.body.lights.toString(16)+'</dd>';
   return '<div class="panel node"><h2>'+esc(n.name)+' node</h2><span class="pill '+cls(st)+'">'+esc(st)+'</span><dl><dt>heartbeat</dt><dd>'+(n.heartbeat_age_ms==null?'–':n.heartbeat_age_ms+' ms ago')+'</dd><dt>firmware</dt><dd>v'+n.fw+(n.trial?' (trial)':'')+'</dd><dt>uptime</dt><dd>'+n.uptime_s+' s</dd>'+extra+'</dl></div>'}).join('');
 if(html)$('nodes').innerHTML=html;
 if(s.recent.length)$('frames').innerHTML=s.recent.map(function(r){var sig=Object.keys(r.signals).filter(function(k){return k!=='ProtoVer'}).map(function(k){return k+'='+r.signals[k]}).join(' ');
   return '<tr><td>'+r.age_ms+' ms</td><td>0x'+r.id.toString(16)+'</td><td>'+esc(r.name)+'</td><td>'+esc(sig)+'</td></tr>'}).join('');
 $('busline').textContent=s.bus.frames+' frames · '+s.bus.rate_fps+' frames/s · '+s.bus.unknown+' unknown · '+s.bus.decode_errors+' decode errors';
 if(s.mode==='sim'){$('sim').hidden=false;if(document.activeElement!==$('mv')){$('mv').value=s.sim.pack_mv;$('mv-v').textContent=(s.sim.pack_mv/1000).toFixed(2)+' V'}$('pwr').checked=s.sim.battery_power}}
function post(body){fetch('/api/sim',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})}
$('mv').addEventListener('input',function(e){$('mv-v').textContent=(e.target.value/1000).toFixed(2)+' V';post({pack_mv:+e.target.value})});
$('pwr').addEventListener('change',function(e){post({battery_power:e.target.checked})});
var es=new EventSource('/events');es.onmessage=function(e){render(JSON.parse(e.data))};
es.onerror=function(){$('live').classList.remove('on');$('src').lastChild.textContent='disconnected, retrying…'};
</script></body></html>"""

if __name__ == "__main__":
    raise SystemExit(main())
