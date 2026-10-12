"""Fault-injection control port for `gateway --sim` (simulation only; it cannot exist on a real bus).

  curl -XPOST localhost:7001/sensor -d '{"node":1,"dc":700}'     # overtemp the battery (0.1 C units)
  curl -XPOST localhost:7001/sensor -d '{"node":1,"mv":2800}'    # undervoltage
  curl -XPOST localhost:7001/power  -d '{"node":1,"on":false}'   # unplug a node from the bus
  curl localhost:7001/state                                        # what the simulated nodes think
"""
from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

def serve(fleet, host: str, port: int) -> ThreadingHTTPServer:
    class H(BaseHTTPRequestHandler):
        def log_message(self, *a): pass

        def _json(self, code, obj):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path != "/state":
                return self._json(404, {"error": "GET /state"})
            self._json(200, {str(i): {"state": n.state_name, "version": n.version, "slot": n.slot, "limp_home": n.limp_home,
                                      "powered": n.powered, "sensors": n.sensors, "boots": n.boots, "wdg_resets": n.wdg_resets}
                             for i, n in fleet.nodes.items()})

        def do_POST(self):
            try:
                req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
                node = fleet.nodes[int(req["node"])]
            except (ValueError, KeyError):
                return self._json(400, {"error": "need JSON with a valid node"})
            if self.path == "/sensor":
                for k in ("mv", "ma", "dc", "ok"):
                    if k in req:
                        node.sensors[k] = int(req[k])
            elif self.path == "/power":
                fleet.set_power(int(req["node"]), bool(req.get("on", True)))
            else:
                return self._json(404, {"error": "POST /sensor or /power"})
            self._json(200, {"ok": True})

    srv = ThreadingHTTPServer((host, port), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv
