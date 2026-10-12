import pytest

from fleetbus import fbp
from fleetbus.impair import ImpairedLink, LinkEnd
from fleetbus.reliable import ReliableSender
from fleetbus.spool import Spool
from harness import RefBackend

def run_stream(n, loss, dup, reorder, seed_spool=None, sender_hook=None):
    now = [0]
    up, down = ImpairedLink(loss, dup, reorder, 20, 10, seed=5), ImpairedLink(loss, dup, reorder, 20, 10, seed=6)
    spool = seed_spool or Spool()
    s = ReliableSender(1, LinkEnd(up, down, lambda: now[0]), spool)
    be = RefBackend(down)
    tx = LinkEnd(up, down, lambda: now[0])
    sent = 0
    while now[0] < 120_000:
        now[0] += 5
        if sent < n and now[0] % 10 == 0:
            s.send(fbp.TELEMETRY, fbp.enc_telemetry(sent, [fbp.CanRecord(0, 0x100, bytes([sent & 0xFF]))]))
            sent += 1
        s.pump(now[0])
        while (raw := tx.recv()) is not None:
            p = fbp.decode(raw)
            if p.type == fbp.ACK:
                s.on_ack(fbp.dec_ack(p.payload))
            elif p.type == fbp.NACK:
                s.on_nack(fbp.dec_nack(p.payload), now[0])
        for raw in up.deliver(now[0]):
            be.on_datagram(raw, now[0])
        if now[0] % 50 == 0 and be.next > 1:
            be.send_ack()
        if sender_hook:
            sender_hook(now[0], s)
        if len(be.delivered) == n and s.unacked == 0:
            break
    return s, be, up

@pytest.mark.parametrize("loss,dup,reorder", [(0, 0, 0), (0.1, 0.05, 0.1), (0.3, 0.1, 0.2), (0.5, 0.2, 0.3)])
def test_every_packet_delivered_once_in_order_under_impairment(loss, dup, reorder):
    n = 500
    s, be, up = run_stream(n, loss, dup, reorder)
    seqs = [p.seq for p in be.delivered]
    assert seqs == list(range(1, n + 1)), f"delivered {len(seqs)}/{n}"
    assert s.unacked == 0
    print(f"\n  loss={loss:.0%} dup={dup:.0%} reorder={reorder:.0%}: {n}/{n} delivered, {s.resent} resends for {s.sent} first sends, {be.dups} dups seen")

def test_retransmitted_packets_carry_the_flag():
    s, be, up = run_stream(50, 0.3, 0, 0)
    assert s.resent > 0

def test_window_bounds_inflight_packets():
    peak = [0]
    def hook(now, s): peak[0] = max(peak[0], len(s.inflight))
    run_stream(300, 0.5, 0, 0, sender_hook=hook)
    assert peak[0] <= 64

def test_spool_survives_restart_and_resumes_without_gaps(tmp_path):
    db = str(tmp_path / "spool.sqlite")
    now = [0]
    up, down = ImpairedLink(0, 0, 0, 10, 0), ImpairedLink(0, 0, 0, 10, 0)
    sp = Spool(db)
    s1 = ReliableSender(1, LinkEnd(up, down, lambda: now[0]), sp)
    for i in range(100):
        s1.send(fbp.TELEMETRY, fbp.enc_telemetry(i, []))
    s1.pump(0)                       # some go out, nothing is acked, then the process "crashes"
    sp.close()
    sp2 = Spool(db)
    assert sp2.count() == 100 and sp2.next_seq() == 101      # nothing lost, numbering continues
    s2 = ReliableSender(1, LinkEnd(up, down, lambda: now[0]), sp2)
    be = RefBackend(down)
    for t in range(0, 20000, 5):
        now[0] = t
        s2.pump(t)
        for raw in up.deliver(t):
            be.on_datagram(raw, t)
        if t % 50 == 0 and be.next > 1:
            be.send_ack()
        for raw in down.deliver(t):
            p = fbp.decode(raw)
            if p.type == fbp.ACK:
                s2.on_ack(fbp.dec_ack(p.payload))
        if len(be.delivered) == 100:
            break
    assert [p.seq for p in be.delivered] == list(range(1, 101))

def test_spool_full_drops_newest_and_counts():
    sp = Spool(max_bytes=200)
    assert sp.put(sp.next_seq(), b"x" * 150)
    assert not sp.put(sp.next_seq(), b"x" * 150)
    assert sp.dropped == 1

def test_spool_ack_frees_space():
    sp = Spool(max_bytes=1000)
    for _ in range(5):
        sp.put(sp.next_seq(), b"x" * 150)
    assert sp.ack_upto(3) == 3 and sp.count() == 2
    assert sp.put(sp.next_seq(), b"x" * 150)
