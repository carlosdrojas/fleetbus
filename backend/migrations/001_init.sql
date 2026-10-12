-- MOCK(M7): draft schema; telemetry partitioning and indexes are decided in M7.
CREATE TABLE nodes (id smallint PRIMARY KEY, name text NOT NULL, hw text NOT NULL, fw_version int, last_seen timestamptz);
CREATE TABLE telemetry (node_id smallint NOT NULL, ts timestamptz NOT NULL, can_id int NOT NULL, data bytea NOT NULL);
CREATE TABLE faults (id bigserial PRIMARY KEY, node_id smallint NOT NULL, ts timestamptz NOT NULL, code int NOT NULL, severity smallint NOT NULL);
CREATE TABLE firmware_images (id serial PRIMARY KEY, node_name text NOT NULL, version int NOT NULL, sha512 bytea NOT NULL, signed_blob bytea NOT NULL);
CREATE TABLE rollouts (id serial PRIMARY KEY, image_id int REFERENCES firmware_images, state text NOT NULL, started_at timestamptz);
CREATE TABLE rollout_steps (rollout_id int REFERENCES rollouts, node_id smallint, state text NOT NULL, at timestamptz);
CREATE TABLE alerts (id bigserial PRIMARY KEY, node_id smallint, rule text NOT NULL, raised_at timestamptz NOT NULL, cleared_at timestamptz);
