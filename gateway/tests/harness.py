"""Test world: simulated fleet -> gateway -> impaired UDP link -> reference backend (Python)."""
from __future__ import annotations

from nacl.signing import SigningKey

from fleetbus import fbp, image
from fleetbus.canio import SimBackend
from fleetbus.gateway import Gateway
from fleetbus.impair import ImpairedLink, LinkEnd
from fleetbus.simfleet import make_sim_fleet
from fleetbus.spool import Spool

KEY = SigningKey(bytes(range(32)))

class RefBackend:
    """Minimal reference receiver: in-order delivery, dedup, cumulative ACK, NACK on gaps, OTA sender. Mirrors the Go
    ingest's contract so the gateway can be tested without a Go process."""
    def __init__(self, tx: ImpairedLink):
        self.tx, self.next, self.held = tx, 1, {}
        self.delivered: list[fbp.Packet] = []
        self.dups = 0
        self.seq = 0
        self.since_ack = 0
        self.statuses: list[tuple] = []
        self.ota = None

    def on_datagram(self, raw: bytes, now: int) -> None:
        try:
            p = fbp.decode(raw)
        except fbp.FbpError:
            return
        if p.seq < self.next or p.seq in self.held:
            self.dups += 1
            self.send_ack()
            return
        self.held[p.seq] = p
        gap = p.seq != self.next
        while self.next in self.held:
            q = self.held.pop(self.next)
            self.delivered.append(q)
            if q.type == fbp.OTA_STATUS:
                st = fbp.dec_ota_status(q.payload)
                self.statuses.append(st)
                if self.ota:
                    self.ota.on_status(*st, now)
            self.next += 1
            self.since_ack += 1
        if gap:
            miss = [s for s in range(self.next, max(self.held)) if s not in self.held][:64]
            if miss:
                self.out(fbp.NACK, fbp.enc_nack(miss))
        if self.since_ack >= 16:
            self.send_ack()

    def send_ack(self):
        self.since_ack = 0
        self.out(fbp.ACK, fbp.enc_ack(self.next - 1))

    def out(self, ptype, payload):
        self.seq += 1
        self.tx.send(fbp.encode(fbp.Packet(ptype, 0, self.seq, payload)))

class OtaServer:
    """Backend-side OTA driver: offer, then stream chunks as the gateway reports next_offset."""
    CHUNK = 900
    def __init__(self, backend: RefBackend, node: int, blob: bytes, slot: int, version: int, image_id=1):
        self.b, self.node, self.blob, self.slot, self.version, self.id = backend, node, blob, slot, version, image_id
        self.sent_to = 0
        self.acked_off = 0
        self.last_activity = -10**9
        self.final = None
        backend.ota = self

    def start(self, now):
        self.b.out(fbp.OTA_OFFER, fbp.enc_ota_offer(self.node, self.slot, self.version, len(self.blob), self.id))
        self.last_activity = now

    def on_status(self, node, status, next_off, detail, now):
        if node != self.node:
            return
        self.last_activity = now
        if status in (fbp.OTA_TRANSFERRED, fbp.OTA_FAILED, fbp.OTA_VERIFY_FAILED):
            self.final = (status, detail)
        else:
            self.acked_off = max(self.acked_off, next_off)
            self.sent_to = max(min(self.sent_to, self.acked_off + 8 * self.CHUNK), self.acked_off) if next_off < self.sent_to else self.sent_to

    def tick(self, now):
        if self.final:
            return
        if now - self.last_activity > 1500:                      # silence: go back to what the gateway last confirmed
            self.sent_to = self.acked_off
            self.last_activity = now
            if self.acked_off == 0 and self.sent_to == 0:
                self.start(now)
        while self.sent_to < len(self.blob) and self.sent_to < self.acked_off + 8 * self.CHUNK:
            d = self.blob[self.sent_to:self.sent_to + self.CHUNK]
            self.b.out(fbp.OTA_CHUNK, fbp.enc_ota_chunk(self.node, self.sent_to, d))
            self.sent_to += len(d)

class World:
    def __init__(self, loss=0.0, dup=0.0, reorder=0.0, delay=10, can_drop_to=0.0, can_drop_from=0.0, spool=":memory:", fleet=None):
        self.now = 0
        self.up = ImpairedLink(loss, dup, reorder, delay, 5, seed=11)
        self.down = ImpairedLink(loss, dup, reorder, delay, 5, seed=12)
        self.fleet = fleet or make_sim_fleet(key=KEY)
        self.can = SimBackend(self.fleet, can_drop_to, can_drop_from)
        self.spool = Spool(spool)
        self.gw = Gateway(1, self.can, LinkEnd(self.up, self.down, lambda: self.now), self.spool, start_ms=self.fleet.now)
        self.now = self.fleet.now
        self.be = RefBackend(self.down)
        self.flush_every = 50
        self._last_flush = 0

    def run(self, ms: int, step: int = 5):
        end = self.now + ms
        while self.now < end:
            self.now += step
            self.gw.poll(self.now)
            for raw in self.up.deliver(self.now):
                self.be.on_datagram(raw, self.now)
            if self.now - self._last_flush >= self.flush_every:     # periodic ack like the real ingest
                self._last_flush = self.now
                if self.be.next > 1:
                    self.be.send_ack()
            if self.be.ota:
                self.be.ota.tick(self.now)

    def telemetry(self):
        out = []
        for p in self.be.delivered:
            if p.type == fbp.TELEMETRY:
                base, recs = fbp.dec_telemetry(p.payload)
                out += [(base + r.dt_ms, r.can_id, r.data) for r in recs]
        return out

    def close(self):
        self.fleet.close()
