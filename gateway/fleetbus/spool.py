"""Durable packet spool (SQLite, WAL). Every outgoing FBP packet is written here before it is sent and deleted when the
backend's cumulative ACK covers it. That makes the gateway crash-safe and gives the disk spool while the link is down."""
from __future__ import annotations

import sqlite3

class Spool:
    def __init__(self, path: str = ":memory:", max_bytes: int = 64 * 1024 * 1024):
        self.db = sqlite3.connect(path, isolation_level=None, check_same_thread=False)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=NORMAL")
        self.db.execute("CREATE TABLE IF NOT EXISTS packets (seq INTEGER PRIMARY KEY, wire BLOB NOT NULL)")
        self.db.execute("CREATE TABLE IF NOT EXISTS meta (k TEXT PRIMARY KEY, v INTEGER NOT NULL)")
        self.max_bytes = max_bytes
        self.dropped = 0
        row = self.db.execute("SELECT v FROM meta WHERE k='last_seq'").fetchone()
        self._last_seq = row[0] if row else 0
        row = self.db.execute("SELECT v FROM meta WHERE k='acked'").fetchone()
        self.acked = row[0] if row else 0
        self._bytes = self.db.execute("SELECT COALESCE(SUM(LENGTH(wire)),0) FROM packets").fetchone()[0]

    def next_seq(self) -> int:
        self._last_seq += 1
        return self._last_seq

    def put(self, seq: int, wire: bytes) -> bool:
        if self._bytes + len(wire) > self.max_bytes:
            self.dropped += 1          # spool full: newest data is dropped and counted (never silent)
            return False
        self.db.execute("BEGIN")
        self.db.execute("INSERT OR REPLACE INTO packets VALUES (?,?)", (seq, wire))
        self.db.execute("INSERT OR REPLACE INTO meta VALUES ('last_seq',?)", (seq,))
        self.db.execute("COMMIT")
        self._bytes += len(wire)
        return True

    def ack_upto(self, cum: int) -> int:
        if cum <= self.acked:
            return 0
        n, b = self.db.execute("SELECT COUNT(*), COALESCE(SUM(LENGTH(wire)),0) FROM packets WHERE seq<=?", (cum,)).fetchone()
        self.db.execute("BEGIN")
        self.db.execute("DELETE FROM packets WHERE seq<=?", (cum,))
        self.db.execute("INSERT OR REPLACE INTO meta VALUES ('acked',?)", (cum,))
        self.db.execute("COMMIT")
        self.acked = cum
        self._bytes -= b
        return n

    def get(self, seq: int) -> bytes | None:
        row = self.db.execute("SELECT wire FROM packets WHERE seq=?", (seq,)).fetchone()
        return row[0] if row else None

    def pending(self, after_seq: int, limit: int) -> list[tuple[int, bytes]]:
        return self.db.execute("SELECT seq, wire FROM packets WHERE seq>? ORDER BY seq LIMIT ?", (after_seq, limit)).fetchall()

    def count(self) -> int:
        return self.db.execute("SELECT COUNT(*) FROM packets").fetchone()[0]

    def close(self) -> None:
        self.db.close()
