"""FleetBus gateway: CAN frames -> FBP telemetry over UDP (reliable, spooled), and OTA relay from backend to nodes."""
from __future__ import annotations

import argparse
import select
import socket
import time

from . import canmsg, fbp
from .canio import CanBackend, PythonCanBackend
from .ota_relay import OtaRelay
from .reliable import ReliableSender, Transport
from .spool import Spool

BATCH_MS = 50
BATCH_MAX = 40
HEARTBEAT_MS = 1000

class UdpTransport(Transport):
    def __init__(self, backend: str, bind_port: int = 0):
        host, port = backend.rsplit(":", 1)
        self.peer = (host, int(port))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("0.0.0.0", bind_port))
        self.sock.setblocking(False)

    def send(self, data: bytes) -> None:
        try:
            self.sock.sendto(data, self.peer)
        except OSError:
            pass                                   # link down: the spool keeps the packet, retransmit timers handle it

    def recv(self) -> bytes | None:
        try:
            return self.sock.recvfrom(2048)[0]
        except (BlockingIOError, OSError):
            return None

class Gateway:
    def __init__(self, gw_id: int, can: CanBackend, transport: Transport, spool: Spool, start_ms: int = 0):
        self.id = gw_id
        self.can, self.tp = can, transport
        self.sender = ReliableSender(gw_id, transport, spool)
        self.ota = OtaRelay(can.send, self._ota_status)
        self.batch: list[tuple[int, int, bytes]] = []
        self.batch_start = 0
        self.last_hb = start_ms
        self.start_ms = start_ms
        self.frames_in = 0
        self.bad_datagrams = 0

    def _ota_status(self, node, status, next_off, detail):
        self.sender.send(fbp.OTA_STATUS, fbp.enc_ota_status(node, status, next_off, detail))

    def poll(self, now_ms: int) -> None:
        for ts, cid, data in self.can.poll(now_ms):
            self.ota.on_can(cid, data, now_ms)
            if cid in canmsg.OTA_IDS:
                continue                            # OTA block traffic is not telemetry
            self.frames_in += 1
            if not self.batch:
                self.batch_start = ts
            self.batch.append((ts, cid, data))
        if self.batch and (now_ms - self.batch_start >= BATCH_MS or len(self.batch) >= BATCH_MAX):
            self._flush()
        while (raw := self.tp.recv()) is not None:
            self._on_datagram(raw, now_ms)
        self.ota.tick(now_ms)
        if now_ms - self.last_hb >= HEARTBEAT_MS:
            self.last_hb = now_ms
            self.sender.send(fbp.HEARTBEAT, fbp.enc_heartbeat((now_ms - self.start_ms) // 1000, min(self.sender.unacked, 65535),
                                                            self.sender.unacked, self.sender.resent))
        self.sender.pump(now_ms)

    def _flush(self) -> None:
        base = self.batch[0][0]
        recs = [fbp.CanRecord(min(ts - base, 0xFFFF), cid, data) for ts, cid, data in self.batch]
        self.batch = []
        self.sender.send(fbp.TELEMETRY, fbp.enc_telemetry(base, recs))

    def _on_datagram(self, raw: bytes, now_ms: int) -> None:
        try:
            p = fbp.decode(raw)
        except fbp.FbpError:
            self.bad_datagrams += 1
            return
        try:
            if p.type == fbp.ACK:
                self.sender.on_ack(fbp.dec_ack(p.payload))
            elif p.type == fbp.NACK:
                self.sender.on_nack(fbp.dec_nack(p.payload), now_ms)
            elif p.type == fbp.OTA_OFFER:
                node, slot, version, size, image_id = fbp.dec_ota_offer(p.payload)
                self.ota.on_offer(node, slot, version, size, image_id, now_ms)
            elif p.type == fbp.OTA_CHUNK:
                node, off, data = fbp.dec_ota_chunk(p.payload)
                self.ota.on_chunk(node, off, data, now_ms)
        except fbp.FbpError:
            self.bad_datagrams += 1

def main() -> None:
    ap = argparse.ArgumentParser(description="FleetBus gateway")
    ap.add_argument("--backend", default="127.0.0.1:9000", help="ingest host:port")
    ap.add_argument("--id", type=int, default=1, help="gateway id (1..255)")
    ap.add_argument("--spool", default="gateway-spool.sqlite")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--sim", action="store_true", help="simulated battery+body nodes (real firmware logic, no hardware)")
    src.add_argument("--can", metavar="INTERFACE:CHANNEL", help="python-can adapter, e.g. slcan:/dev/tty.usbmodem1 or gs_usb:0")
    ap.add_argument("--bitrate", type=int, default=500000)
    ap.add_argument("--read-only", action="store_true", help="never transmit on the bus (use for anything near the car)")
    ap.add_argument("--duration", type=float, default=0, help="stop after N seconds (0 = run forever)")
    a = ap.parse_args()

    if a.sim:
        from .simfleet import make_sim_fleet
        from .canio import SimBackend
        can = SimBackend(make_sim_fleet())
    else:
        iface, channel = a.can.split(":", 1)
        can = PythonCanBackend(iface, channel, a.bitrate, read_only=a.read_only)
    t0 = time.monotonic()
    ms = lambda: int((time.monotonic() - t0) * 1000)
    gw = Gateway(a.id, can, UdpTransport(a.backend), Spool(a.spool), start_ms=ms())
    print(f"gateway {a.id} -> {a.backend} ({'sim' if a.sim else a.can}); Ctrl-C to stop")
    try:
        while a.duration == 0 or time.monotonic() - t0 < a.duration:
            gw.poll(ms())
            select.select([gw.tp.sock], [], [], 0.005)
    except KeyboardInterrupt:
        pass

if __name__ == "__main__":
    main()
