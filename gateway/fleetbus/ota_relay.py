"""Gateway side of OTA: receives an image from the backend as OTA_CHUNKs (FBP) and pushes it to one node over CAN as
6-byte blocks with a sliding window, resending from the node's NextBlock on loss. Reports OTA_STATUS to the backend,
which drives the transfer (it resends chunks from the next_offset we report)."""
from __future__ import annotations

from . import canmsg, fbp

BLOCK = 6
WINDOW_BLOCKS = 16
ACK_TIMEOUT_MS = 400
BEGIN_TIMEOUT_MS = 3000
FINISH_TIMEOUT_MS = 3000
MAX_RETRIES = 5

IDLE, BEGINNING, SENDING, FINISHING = "idle", "beginning", "sending", "finishing"

class OtaRelay:
    MAX_AHEAD = 64 * 1024

    def __init__(self, can_send, report):
        self.can_send = can_send              # (can_id, data) -> None
        self.report = report                  # (node, status, next_offset, detail) -> None
        self.reset()

    def reset(self):
        self.state = IDLE
        self.node = 0
        self.slot = 0
        self.size = 0
        self.total_blocks = 0
        self.buf = bytearray()
        self.pending: dict[int, bytes] = {}    # out-of-order chunks (UDP may reorder)
        self.acked = 0                        # blocks the node has acknowledged
        self.sent_upto = 0                    # blocks transmitted at least once
        self.deadline = 0
        self.retries = 0
        self.image_id = 0
        self.last_report_off = -1

    # ---- from the backend (FBP)
    def on_offer(self, node, slot, version, size, image_id, now_ms):
        if size == 0:                                   # cancel
            if self.state != IDLE and node == self.node:
                self.can_send(*canmsg.encode("OtaCtrl", Cmd=2, TotalBlocks=0, Slot=self.slot, TargetNode=self.node))
                self.reset()
            return
        if self.state != IDLE and image_id == self.image_id and node == self.node:
            self._report(fbp.OTA_PROGRESS, now_ms)      # duplicate offer: just re-report where we are
            return
        self.reset()
        self.node, self.slot, self.size, self.image_id = node, slot, size, image_id
        self.total_blocks = (size + BLOCK - 1) // BLOCK
        self._begin(now_ms)

    def on_chunk(self, node, offset, data, now_ms):
        if self.state == IDLE or node != self.node:
            return
        before = len(self.buf)
        if offset == len(self.buf):
            self.buf += data
            while len(self.buf) in self.pending:
                self.buf += self.pending.pop(len(self.buf))
        elif offset > len(self.buf) and offset - len(self.buf) <= self.MAX_AHEAD and len(self.pending) < 64:
            self.pending[offset] = data
        self._report(fbp.OTA_PROGRESS, now_ms, force=len(self.buf) == before)   # a stuck stream re-reports so the backend resends

    # ---- from the CAN bus
    def on_can(self, can_id, data, now_ms):
        if can_id != canmsg.OTA_ACK or self.state == IDLE:
            return
        dec = canmsg.decode(can_id, data)
        if not dec:
            return
        a = dec[1]
        if a["NodeId"] != self.node:
            return
        st, nxt = a["Status"], a["NextBlock"]
        if self.state == BEGINNING:
            if st == 0:
                self.state, self.acked, self.sent_upto, self.retries = SENDING, 0, 0, 0
                self.deadline = now_ms + ACK_TIMEOUT_MS
                self._report(fbp.OTA_ACCEPTED, now_ms, force=True)
            else:
                self._fail(fbp.OTA_FAILED, st, now_ms)
        elif self.state == SENDING:
            if st == 0:
                if nxt > self.acked:
                    self.acked, self.retries = nxt, 0
                    self.deadline = now_ms + ACK_TIMEOUT_MS
            elif st == 1:                                  # node wants a different block: rewind
                self.acked = self.sent_upto = nxt
            else:
                self._fail(fbp.OTA_FAILED, st, now_ms)
        elif self.state == FINISHING:
            if st == 3:
                self._report(fbp.OTA_TRANSFERRED, now_ms, force=True)
                self.reset()
            elif st == 2:
                self._fail(fbp.OTA_VERIFY_FAILED, st, now_ms)
            else:
                self._fail(fbp.OTA_FAILED, st, now_ms)

    # ---- time
    def tick(self, now_ms):
        if self.state == BEGINNING and now_ms >= self.deadline:
            self.retries += 1
            if self.retries > MAX_RETRIES:
                return self._fail(fbp.OTA_FAILED, 0xFF, now_ms)
            self._begin(now_ms, keep_retries=True)
        elif self.state == SENDING:
            avail = len(self.buf) // BLOCK + (1 if len(self.buf) >= self.size and len(self.buf) % BLOCK else 0)
            avail = min(avail, self.total_blocks)
            while self.sent_upto < avail and self.sent_upto < self.acked + WINDOW_BLOCKS:
                self._send_block(self.sent_upto)
                self.sent_upto += 1
            if self.acked >= self.total_blocks:
                self._finish(now_ms)
            elif now_ms >= self.deadline and self.sent_upto > self.acked:
                self.retries += 1
                if self.retries > MAX_RETRIES:
                    return self._fail(fbp.OTA_FAILED, 0xFE, now_ms)
                self.sent_upto = self.acked                # resend the window from the last acked block
                self.deadline = now_ms + ACK_TIMEOUT_MS
            elif self.sent_upto == self.acked:
                self.deadline = now_ms + ACK_TIMEOUT_MS    # starved waiting for the backend, not a CAN fault
        elif self.state == FINISHING and now_ms >= self.deadline:
            self.retries += 1
            if self.retries > MAX_RETRIES:
                return self._fail(fbp.OTA_FAILED, 0xFD, now_ms)
            self._finish(now_ms, keep_retries=True)

    # ---- internals
    def _ctrl(self, cmd):
        self.can_send(*canmsg.encode("OtaCtrl", Cmd=cmd, TotalBlocks=self.total_blocks, Slot=self.slot, TargetNode=self.node))

    def _begin(self, now_ms, keep_retries=False):
        self.state = BEGINNING
        if not keep_retries:
            self.retries = 0
        self.deadline = now_ms + BEGIN_TIMEOUT_MS
        self._ctrl(1)

    def _finish(self, now_ms, keep_retries=False):
        self.state = FINISHING
        if not keep_retries:
            self.retries = 0
        self.deadline = now_ms + FINISH_TIMEOUT_MS
        self._ctrl(3)

    def _send_block(self, b):
        chunk = bytes(self.buf[b * BLOCK:(b + 1) * BLOCK]).ljust(BLOCK, b"\xff")
        self.can_send(*canmsg.encode("OtaData", Block=b, **{f"D{i}": chunk[i] for i in range(BLOCK)}))

    def _fail(self, status, detail, now_ms):
        self.report(self.node, status, len(self.buf), detail)
        if self.state != IDLE:
            self.can_send(*canmsg.encode("OtaCtrl", Cmd=2, TotalBlocks=0, Slot=self.slot, TargetNode=self.node))
        self.reset()

    def _report(self, status, now_ms, force=False):
        if force or len(self.buf) != self.last_report_off:
            self.last_report_off = len(self.buf)
            self.report(self.node, status, len(self.buf), 0)
