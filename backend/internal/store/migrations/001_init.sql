-- FleetBus schema v1. Applied by store.Migrate (embedded, ordered, idempotent via schema_migrations).

CREATE TABLE gateways (
    id          smallint PRIMARY KEY,
    addr        text,
    simulated   boolean NOT NULL DEFAULT false,
    first_seen  timestamptz NOT NULL,
    last_seen   timestamptz NOT NULL,
    uptime_s    bigint NOT NULL DEFAULT 0,
    unacked     integer NOT NULL DEFAULT 0,
    spooled     bigint NOT NULL DEFAULT 0,
    resent      bigint NOT NULL DEFAULT 0
);

CREATE TABLE nodes (
    gateway_id  smallint NOT NULL REFERENCES gateways(id),
    node_id     smallint NOT NULL,                 -- role: 1 battery, 2 body, 3 telematics
    name        text NOT NULL,
    fw_version  integer NOT NULL,
    slot        smallint NOT NULL,
    trial       boolean NOT NULL,
    state       smallint NOT NULL,
    first_seen  timestamptz NOT NULL,
    last_seen   timestamptz NOT NULL,
    PRIMARY KEY (gateway_id, node_id)
);

-- Raw CAN frames, daily range partitions (created by ensure_telemetry_partition) with a BRIN index on ts.
CREATE TABLE telemetry (
    ts          timestamptz NOT NULL,
    gateway_id  smallint NOT NULL,
    node_id     smallint,
    can_id      integer NOT NULL,
    data        bytea NOT NULL
) PARTITION BY RANGE (ts);
CREATE TABLE telemetry_default PARTITION OF telemetry DEFAULT;
CREATE INDEX telemetry_ts_brin ON telemetry USING brin (ts);

CREATE FUNCTION ensure_telemetry_partition(day date) RETURNS void AS $$
DECLARE
    name text := 'telemetry_' || to_char(day, 'YYYYMMDD');
BEGIN
    IF to_regclass(name) IS NULL THEN
        EXECUTE format('CREATE TABLE %I PARTITION OF telemetry FOR VALUES FROM (%L) TO (%L)', name, day, day + 1);
    END IF;
END $$ LANGUAGE plpgsql;

-- Decoded signals for charts and health checks.
CREATE TABLE metrics (
    ts          timestamptz NOT NULL,
    gateway_id  smallint NOT NULL,
    node_id     smallint NOT NULL,
    name        text NOT NULL,
    value       double precision NOT NULL
);
CREATE INDEX metrics_lookup ON metrics (gateway_id, node_id, name, ts DESC);
CREATE INDEX metrics_ts_brin ON metrics USING brin (ts);

CREATE TABLE faults (
    id          bigserial PRIMARY KEY,
    ts          timestamptz NOT NULL,
    gateway_id  smallint NOT NULL,
    node_id     smallint NOT NULL,
    code        integer NOT NULL,
    severity    smallint NOT NULL
);
CREATE INDEX faults_node_ts ON faults (gateway_id, node_id, ts DESC);

CREATE TABLE node_events (
    id          bigserial PRIMARY KEY,
    ts          timestamptz NOT NULL DEFAULT now(),
    gateway_id  smallint NOT NULL,
    node_id     smallint NOT NULL,
    kind        text NOT NULL,                      -- first_seen | state | version | slot | trial
    detail      jsonb NOT NULL DEFAULT '{}'
);
CREATE INDEX node_events_node ON node_events (gateway_id, node_id, ts DESC);

CREATE TABLE firmware_images (
    id          serial PRIMARY KEY,
    node_id     smallint NOT NULL,
    version     integer NOT NULL,
    blob_a      bytea NOT NULL,                     -- signed image linked for slot A
    blob_b      bytea NOT NULL,                     -- signed image linked for slot B
    sha256_a    text NOT NULL,
    sha256_b    text NOT NULL,
    notes       text NOT NULL DEFAULT '',
    created_at  timestamptz NOT NULL DEFAULT now(),
    UNIQUE (node_id, version)
);

CREATE TABLE rollouts (
    id               serial PRIMARY KEY,
    image_id         integer NOT NULL REFERENCES firmware_images(id),
    state            text NOT NULL,                 -- pending | running | succeeded | failed | aborted
    canary_gateway   smallint,
    health_window_ms integer NOT NULL DEFAULT 30000,
    soak_ms          integer NOT NULL DEFAULT 3000,
    created_at       timestamptz NOT NULL DEFAULT now(),
    started_at       timestamptz,
    finished_at      timestamptz,
    failure_reason   text
);

CREATE TABLE rollout_steps (
    id             serial PRIMARY KEY,
    rollout_id     integer NOT NULL REFERENCES rollouts(id),
    ordinal        integer NOT NULL,
    gateway_id     smallint NOT NULL,
    node_id        smallint NOT NULL,
    phase          text NOT NULL,                   -- canary | fleet
    state          text NOT NULL,                   -- pending | transferring | verifying | healthy | failed | rolled_back | skipped
    target_slot    smallint,
    prev_version   integer,
    seen_target    boolean NOT NULL DEFAULT false,
    started_at     timestamptz,
    transferred_at timestamptz,
    healthy_since  timestamptz,
    updated_at     timestamptz NOT NULL DEFAULT now(),
    detail         text NOT NULL DEFAULT '',
    UNIQUE (rollout_id, ordinal)
);

CREATE TABLE ota_transfers (
    id           serial PRIMARY KEY,
    step_id      integer NOT NULL REFERENCES rollout_steps(id),
    gateway_id   smallint NOT NULL,
    node_id      smallint NOT NULL,
    slot         smallint NOT NULL,
    version      integer NOT NULL,
    state        text NOT NULL,                     -- queued | active | transferred | failed | verify_failed | cancelled
    size         integer NOT NULL,
    next_offset  integer NOT NULL DEFAULT 0,
    detail       text NOT NULL DEFAULT '',
    created_at   timestamptz NOT NULL DEFAULT now(),
    updated_at   timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE alerts (
    id          bigserial PRIMARY KEY,
    rule        text NOT NULL,
    gateway_id  smallint NOT NULL DEFAULT 0,
    node_id     smallint NOT NULL DEFAULT 0,
    severity    text NOT NULL,                      -- info | warning | critical
    message     text NOT NULL,
    raised_at   timestamptz NOT NULL DEFAULT now(),
    cleared_at  timestamptz
);
CREATE UNIQUE INDEX alerts_one_open ON alerts (rule, gateway_id, node_id) WHERE cleared_at IS NULL;
