"""Network impairment for tests and demos: loss, duplication, reordering and delay on a datagram path."""
from __future__ import annotations

import heapq
import random

class ImpairedLink:
    """One direction. send() is called by the producer; deliver(now) returns datagrams due at `now`."""
    def __init__(self, loss=0.0, dup=0.0, reorder=0.0, delay_ms=0, jitter_ms=0, seed=1):
        self.loss, self.dup, self.reorder, self.delay, self.jitter = loss, dup, reorder, delay_ms, jitter_ms
        self.rng = random.Random(seed)
        self.q: list[tuple[float, int, bytes]] = []
        self.n = 0
        self.now = 0
        self.sent = self.lost = self.duplicated = 0

    def send(self, data: bytes) -> None:
        self.sent += 1
        if self.rng.random() < self.loss:
            self.lost += 1
            return
        copies = 1 + (self.rng.random() < self.dup)
        self.duplicated += copies - 1
        for _ in range(copies):
            extra = self.rng.uniform(0, self.jitter) + (self.rng.uniform(20, 120) if self.rng.random() < self.reorder else 0)
            self.n += 1
            heapq.heappush(self.q, (self.now + self.delay + extra, self.n, data))

    def deliver(self, now_ms: int) -> list[bytes]:
        self.now = now_ms
        out = []
        while self.q and self.q[0][0] <= now_ms:
            out.append(heapq.heappop(self.q)[2])
        return out

class LinkEnd:
    """Gateway-facing end of an ImpairedLink pair, usable as a reliable.Transport."""
    def __init__(self, tx: ImpairedLink, rx: ImpairedLink, clock):
        self.tx, self.rx, self.clock, self.buf = tx, rx, clock, []
    def send(self, data: bytes) -> None:
        self.tx.send(data)
    def recv(self):
        if not self.buf:
            self.buf = self.rx.deliver(self.clock())
        return self.buf.pop(0) if self.buf else None
