package store

import "time"

type Gateway struct {
	ID        int16     `json:"id"`
	Addr      string    `json:"addr"`
	Simulated bool      `json:"simulated"`
	FirstSeen time.Time `json:"first_seen"`
	LastSeen  time.Time `json:"last_seen"`
	UptimeS   int64     `json:"uptime_s"`
	Unacked   int32     `json:"unacked"`
	Spooled   int64     `json:"spooled"`
	Resent    int64     `json:"resent"`
}

type Node struct {
	GatewayID int16     `json:"gateway_id"`
	NodeID    int16     `json:"node_id"`
	Name      string    `json:"name"`
	FwVersion int32     `json:"fw_version"`
	Slot      int16     `json:"slot"`
	Trial     bool      `json:"trial"`
	State     int16     `json:"state"`
	FirstSeen time.Time `json:"first_seen"`
	LastSeen  time.Time `json:"last_seen"`
}

type TelRow struct {
	TS        time.Time
	GatewayID int16
	NodeID    *int16
	CanID     int32
	Data      []byte
}

type MetricRow struct {
	TS        time.Time `json:"ts"`
	GatewayID int16     `json:"gateway_id"`
	NodeID    int16     `json:"node_id"`
	Name      string    `json:"name"`
	Value     float64   `json:"value"`
}

type FaultRow struct {
	ID        int64     `json:"id"`
	TS        time.Time `json:"ts"`
	GatewayID int16     `json:"gateway_id"`
	NodeID    int16     `json:"node_id"`
	Code      int32     `json:"code"`
	Severity  int16     `json:"severity"`
}

type NodeHeartbeat struct {
	TS        time.Time
	GatewayID int16
	NodeID    int16
	FwVersion int32
	Slot      int16
	Trial     bool
	State     int16
	UptimeS   int32
}

type GatewayUpdate struct {
	ID        int16
	Addr      string
	TS        time.Time
	HasStats  bool
	UptimeS   int64
	Unacked   int32
	Spooled   int64
	Resent    int64
	Simulated bool
}

type OtaStatusRow struct {
	GatewayID  int16
	NodeID     int16
	Status     uint8
	NextOffset int32
	Detail     int32
}

// Batch is everything the ingest writer commits in one transaction.
type Batch struct {
	Gateways    []GatewayUpdate
	Telemetry   []TelRow
	Metrics     []MetricRow
	Faults      []FaultRow
	Heartbeats  []NodeHeartbeat
	OtaStatuses []OtaStatusRow
}

type NodeEvent struct {
	ID        int64          `json:"id"`
	TS        time.Time      `json:"ts"`
	GatewayID int16          `json:"gateway_id"`
	NodeID    int16          `json:"node_id"`
	Kind      string         `json:"kind"`
	Detail    map[string]any `json:"detail"`
}

type Alert struct {
	ID        int64      `json:"id"`
	Rule      string     `json:"rule"`
	GatewayID int16      `json:"gateway_id"`
	NodeID    int16      `json:"node_id"`
	Severity  string     `json:"severity"`
	Message   string     `json:"message"`
	RaisedAt  time.Time  `json:"raised_at"`
	ClearedAt *time.Time `json:"cleared_at"`
}

type Image struct {
	ID        int32     `json:"id"`
	NodeID    int16     `json:"node_id"`
	Version   int32     `json:"version"`
	SHA256A   string    `json:"sha256_a"`
	SHA256B   string    `json:"sha256_b"`
	Notes     string    `json:"notes"`
	CreatedAt time.Time `json:"created_at"`
	SizeA     int32     `json:"size_a"`
}

type Rollout struct {
	ID            int32      `json:"id"`
	ImageID       int32      `json:"image_id"`
	NodeID        int16      `json:"node_id"`
	Version       int32      `json:"version"`
	State         string     `json:"state"`
	CanaryGateway *int16     `json:"canary_gateway"`
	HealthWindow  int32      `json:"health_window_ms"`
	SoakMS        int32      `json:"soak_ms"`
	CreatedAt     time.Time  `json:"created_at"`
	StartedAt     *time.Time `json:"started_at"`
	FinishedAt    *time.Time `json:"finished_at"`
	FailureReason *string    `json:"failure_reason"`
	Steps         []Step     `json:"steps,omitempty"`
}

type Step struct {
	ID            int32      `json:"id"`
	RolloutID     int32      `json:"rollout_id"`
	Ordinal       int32      `json:"ordinal"`
	GatewayID     int16      `json:"gateway_id"`
	NodeID        int16      `json:"node_id"`
	Phase         string     `json:"phase"`
	State         string     `json:"state"`
	TargetSlot    *int16     `json:"target_slot"`
	PrevVersion   *int32     `json:"prev_version"`
	SeenTarget    bool       `json:"seen_target"`
	StartedAt     *time.Time `json:"started_at"`
	TransferredAt *time.Time `json:"transferred_at"`
	HealthySince  *time.Time `json:"healthy_since"`
	UpdatedAt     time.Time  `json:"updated_at"`
	Detail        string     `json:"detail"`
}

type Transfer struct {
	ID         int32     `json:"id"`
	StepID     int32     `json:"step_id"`
	GatewayID  int16     `json:"gateway_id"`
	NodeID     int16     `json:"node_id"`
	Slot       int16     `json:"slot"`
	Version    int32     `json:"version"`
	State      string    `json:"state"`
	Size       int32     `json:"size"`
	NextOffset int32     `json:"next_offset"`
	Detail     string    `json:"detail"`
	UpdatedAt  time.Time `json:"updated_at"`
}

var NodeNames = map[int16]string{1: "battery", 2: "body", 3: "telematics"}
