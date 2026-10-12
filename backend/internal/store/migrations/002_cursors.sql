-- Per-gateway committed sequence number, written in the same transaction as the data it covers, so an ingest restart
-- resumes exactly where the database is (retransmitted packets at or below the cursor are acknowledged, not re-stored).
CREATE TABLE gateway_cursors (
    gateway_id    smallint PRIMARY KEY,
    committed_seq bigint NOT NULL,
    updated_at    timestamptz NOT NULL DEFAULT now()
);
