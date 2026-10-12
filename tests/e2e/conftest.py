"""End-to-end stack: Postgres (docker compose, host port 5433) + ingest + api + N simulated cars (gateway --sim).
Everything real except the CAN bus and the MCU, which are the host simulator running the REAL firmware logic.
Run:  docker compose -f deploy/docker-compose.yml up -d db && make e2e   (or: pytest tests/e2e -m e2e)"""
from __future__ import annotations

import json
import os
import signal
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

import psycopg
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "gateway"))
BIN = ROOT / "build" / "bin"
ADMIN_DSN = os.environ.get("FLEETBUS_E2E_ADMIN_DSN", "postgresql://fleetbus:fleetbus@localhost:5433/fleetbus")

def free_port(udp=False):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p

def wait_until(cond, timeout=20.0, interval=0.2, what="condition"):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        try:
            r = cond()
            if r:
                return r
        except (urllib.error.URLError, ConnectionError, KeyError, IndexError, TypeError) as e:
            last = e
        time.sleep(interval)
    raise AssertionError(f"timeout ({timeout}s) waiting for {what}" + (f" (last error: {last})" if last else ""))

class Stack:
    def __init__(self, tmp: Path, cars: int = 3):
        self.tmp, self.cars = tmp, cars
        self.db_name = "fbe2e_" + uuid.uuid4().hex[:8]
        with psycopg.connect(ADMIN_DSN, autocommit=True) as c:
            c.execute(f"CREATE DATABASE {self.db_name}")
        self.dsn = ADMIN_DSN.rsplit("/", 1)[0] + "/" + self.db_name + "?sslmode=disable"
        self.udp_port, self.http_port = free_port(True), free_port()
        self.ctl_ports = {g: free_port() for g in range(1, cars + 1)}
        self.procs: dict[str, subprocess.Popen] = {}
        self.pubkey = self.sim_pubkey()

    @staticmethod
    def sim_pubkey() -> str:
        from fleetbus.simfleet import sim_key
        return bytes(sim_key().verify_key).hex()

    # ---- processes
    def _spawn(self, name, cmd, **kw):
        log = open(self.tmp / f"{name}.log", "ab")
        self.procs[name] = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, **kw)

    def start_ingest(self):
        self._spawn("ingest", [str(BIN / "ingest"), "-listen", f"127.0.0.1:{self.udp_port}", "-metrics", "", "-dsn", self.dsn])

    def start_api(self):
        self._spawn("api", [str(BIN / "api"), "-listen", f"127.0.0.1:{self.http_port}", "-dsn", self.dsn, "-pubkey", self.pubkey, "-node-silent-after", "2s"])
        wait_until(lambda: self.get("/healthz", raw=True), 15, what="api up")

    def start_gateway(self, g):
        self._spawn(f"gw{g}", [sys.executable, "-m", "fleetbus.gateway", "--sim", "--id", str(g), "--backend", f"127.0.0.1:{self.udp_port}",
                               "--spool", str(self.tmp / f"spool{g}.sqlite"), "--control", f"127.0.0.1:{self.ctl_ports[g]}"], cwd=ROOT / "gateway")

    def kill(self, name, sig=signal.SIGKILL):
        p = self.procs.pop(name, None)
        if p:
            p.send_signal(sig)
            p.wait(10)

    def stop_all(self):
        for n in list(self.procs):
            self.kill(n, signal.SIGTERM)

    # ---- http
    def get(self, path, raw=False):
        with urllib.request.urlopen(f"http://127.0.0.1:{self.http_port}{path}", timeout=5) as r:
            body = r.read()
        return body if raw else json.loads(body)

    def fbctl(self, *args, check=True):
        r = subprocess.run([str(BIN / "fbctl"), *args], capture_output=True, text=True, env={**os.environ, "FLEETBUS_API": f"http://127.0.0.1:{self.http_port}"})
        if check and r.returncode != 0:
            raise AssertionError(f"fbctl {' '.join(args)} failed: {r.stdout}{r.stderr}")
        return r

    def control(self, g, path, body):
        req = urllib.request.Request(f"http://127.0.0.1:{self.ctl_ports[g]}{path}", json.dumps(body).encode(), {"Content-Type": "application/json"})
        return json.loads(urllib.request.urlopen(req, timeout=5).read())

    def sim_state(self, g):
        return json.loads(urllib.request.urlopen(f"http://127.0.0.1:{self.ctl_ports[g]}/state", timeout=5).read())

    # ---- domain helpers
    def fleet(self):
        return {g["id"]: g for g in self.get("/api/fleet")["gateways"]}

    def node(self, gw, name):
        return next(n for n in self.fleet()[gw]["nodes"] if n["name"] == name)

    def alerts(self, all_=False):
        return self.get("/api/alerts" + ("?state=all" if all_ else ""))

    def sql(self, q, *a):
        with psycopg.connect(self.dsn) as c:
            return c.execute(q, a).fetchall()

    def upload(self, version, behaviour=0, body_len=2000, node="body"):
        """Build both slot images signed with the sim key, upload through fbctl. Returns the image id."""
        from fleetbus.image import NODES, build_image
        from fleetbus.simfleet import sim_key
        key = sim_key()
        body = bytes([behaviour, version & 0xFF]) + b"D" * body_len
        paths = []
        for slot, letter in ((0, "A"), (1, "B")):
            p = self.tmp / f"{node}_v{version}_{behaviour}_{letter}.signed"
            p.write_bytes(build_image(body, NODES[node], slot, version, key))
            paths.append(str(p))
        out = self.fbctl("image", "upload", "-a", paths[0], "-b", paths[1], "-notes", f"e2e v{version}").stdout
        return int(out.split("image id ")[1].split()[0])

    def rollout(self, rid):
        return self.get(f"/api/rollouts/{rid}")

    def start_rollout(self, image, canary=None, window="30s", soak="2s"):
        args = ["rollout", "start", "-image", str(image), "-window", window, "-soak", soak]
        if canary:
            args += ["-canary", str(canary)]
        return int(self.fbctl(*args).stdout.split()[1])

    def wait_rollout(self, rid, states=("succeeded", "failed", "aborted"), timeout=90):
        return wait_until(lambda: (r := self.rollout(rid))["rollout"]["state"] in states and r, timeout, 0.5, f"rollout {rid} to finish")

    def dump_logs(self):
        for f in sorted(self.tmp.glob("*.log")):
            tail = f.read_text(errors="replace").splitlines()[-15:]
            print(f"--- {f.name}\n" + "\n".join(tail))

    def teardown(self):
        self.stop_all()
        time.sleep(0.3)
        with psycopg.connect(ADMIN_DSN, autocommit=True) as c:
            c.execute(f"DROP DATABASE IF EXISTS {self.db_name} WITH (FORCE)")

def pytest_configure(config):
    config.addinivalue_line("markers", "e2e: end-to-end tests that start real processes and need docker postgres on :5433")

@pytest.fixture(scope="session")
def stack(tmp_path_factory):
    # Always rebuild (incremental, ~1 s): a stale binary silently tests yesterday's code.
    for b in ("ingest", "api", "fbctl"):
        subprocess.run(["go", "build", "-o", str(BIN / b), f"./cmd/{b}"], cwd=ROOT / "backend", check=True)
    try:
        psycopg.connect(ADMIN_DSN, connect_timeout=3).close()
    except Exception as e:  # noqa: BLE001
        if os.environ.get("FLEETBUS_REQUIRE_DB"):
            pytest.fail(f"postgres required: {e}")
        pytest.skip(f"postgres not reachable: {e}")
    s = Stack(tmp_path_factory.mktemp("e2e"))
    s.start_ingest()
    s.start_api()
    for g in s.ctl_ports:
        s.start_gateway(g)
    wait_until(lambda: len(s.fleet()) == s.cars and all(len(g["nodes"]) == 2 for g in s.fleet().values()), 30, what="3 cars with 2 nodes each")
    yield s
    s.stop_all()
    s.teardown()

@pytest.fixture(autouse=True)
def _logs_on_failure(request):
    yield
    if request.node.rep_call.failed if hasattr(request.node, "rep_call") else False:
        request.getfixturevalue("stack").dump_logs()

@pytest.hookimpl(hookwrapper=True, tryfirst=True)
def pytest_runtest_makereport(item, call):
    out = yield
    setattr(item, "rep_" + call.when, out.get_result())
