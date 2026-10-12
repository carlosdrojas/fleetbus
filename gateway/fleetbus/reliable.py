"""Reliable sender for gateway -> backend FBP: spool-backed, windowed, retransmitting. See protocol/spec.md."""
from __future__ import annotations

from . import fbp
from .spool import Spool

class Transport:
    """Datagram transport interface."""
    def send(self, data: bytes) -> None: ...
    def recv(self) -> bytes | None: ...

class ReliableSender:
    def __init__(self, src: int, transport: Transport, spool: Spool, window: int = 64, rto_ms: int = 250, max_rto_ms: int = 4000, nack_damp_ms: int = 100):
        self.src, self.tp, self.spool = src, transport, spool
        self.window, self.rto, self.max_rto, self.nack_damp_ms = window, rto_ms, max_rto_ms, nack_damp_ms
        self.sent_upto = spool.acked            # highest seq transmitted at least once (restart: resend everything unacked)
        self.inflight: dict[int, list[int]] = {}   # seq -> [last_sent_ms, tries]
        self.resent = 0
        self.sent = 0

    # ---- producing
    def send(self, ptype: int, payload: bytes) -> int | None:
        seq = self.spool.next_seq()
        wire = fbp.encode(fbp.Packet(ptype, self.src, seq, payload))
        return seq if self.spool.put(seq, wire) else None

    # ---- feedback from the backend
    def on_ack(self, cum: int) -> None:
        self.spool.ack_upto(cum)
        for s in [s for s in self.inflight if s <= cum]:
            del self.inflight[s]

    def on_nack(self, seqs: list[int], now_ms: int) -> None:
        for s in seqs:
            rec = self.inflight.get(s)
            if rec is not None and now_ms - rec[0] < self.nack_damp_ms:
                continue                    # we just (re)sent it; the NACK was probably generated before it arrived
            wire = self.spool.get(s)
            if wire is not None:
                self._transmit(s, wire, now_ms, retransmit=True)

    # ---- driving
    def pump(self, now_ms: int) -> None:
        for seq, rec in list(self.inflight.items()):               # timeouts, exponential backoff
            last, tries = rec
            if now_ms - last >= min(self.rto * (2 ** tries), self.max_rto):
                wire = self.spool.get(seq)
                if wire is None:
                    del self.inflight[seq]
                    continue
                self._transmit(seq, wire, now_ms, retransmit=True)
        while len(self.inflight) < self.window:                    # fill the window from the spool
            nxt = self.spool.pending(self.sent_upto, self.window - len(self.inflight))
            if not nxt:
                break
            for seq, wire in nxt:
                self._transmit(seq, wire, now_ms, retransmit=False)
                self.sent_upto = seq

    def _transmit(self, seq: int, wire: bytes, now_ms: int, retransmit: bool) -> None:
        if retransmit:
            wire = fbp.set_retransmit(wire)
            self.resent += 1
            rec = self.inflight.setdefault(seq, [now_ms, 0])
            rec[0] = now_ms
            rec[1] = min(rec[1] + 1, 10)
        else:
            self.inflight[seq] = [now_ms, 0]
            self.sent += 1
        self.tp.send(wire)

    @property
    def unacked(self) -> int:
        return self.spool.count()
