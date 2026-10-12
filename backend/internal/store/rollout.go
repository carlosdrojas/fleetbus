package store

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
)

// ---- firmware images ----------------------------------------------------------------------------------------

func (p *PG) InsertImage(ctx context.Context, node int16, version int32, blobA, blobB []byte, notes string) (int32, error) {
	sa, sb := sha256.Sum256(blobA), sha256.Sum256(blobB)
	var id int32
	err := p.Pool.QueryRow(ctx, `INSERT INTO firmware_images(node_id, version, blob_a, blob_b, sha256_a, sha256_b, notes) VALUES ($1,$2,$3,$4,$5,$6,$7) RETURNING id`,
		node, version, blobA, blobB, hex.EncodeToString(sa[:]), hex.EncodeToString(sb[:]), notes).Scan(&id)
	return id, err
}

func (p *PG) ListImages(ctx context.Context) ([]Image, error) {
	rows, err := p.Pool.Query(ctx, `SELECT id, node_id, version, sha256_a, sha256_b, notes, created_at, length(blob_a) FROM firmware_images ORDER BY id DESC`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Image
	for rows.Next() {
		var i Image
		if err := rows.Scan(&i.ID, &i.NodeID, &i.Version, &i.SHA256A, &i.SHA256B, &i.Notes, &i.CreatedAt, &i.SizeA); err != nil {
			return nil, err
		}
		out = append(out, i)
	}
	return out, rows.Err()
}

func (p *PG) GetImage(ctx context.Context, id int32) (*Image, error) {
	var i Image
	err := p.Pool.QueryRow(ctx, `SELECT id, node_id, version, sha256_a, sha256_b, notes, created_at, length(blob_a) FROM firmware_images WHERE id=$1`, id).
		Scan(&i.ID, &i.NodeID, &i.Version, &i.SHA256A, &i.SHA256B, &i.Notes, &i.CreatedAt, &i.SizeA)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, nil
	}
	return &i, err
}

// ---- rollouts -----------------------------------------------------------------------------------------------

const rolloutCols = `r.id, r.image_id, i.node_id, i.version, r.state, r.canary_gateway, r.health_window_ms, r.soak_ms, r.created_at, r.started_at, r.finished_at, r.failure_reason`

func scanRollout(row pgx.Row) (*Rollout, error) {
	var r Rollout
	err := row.Scan(&r.ID, &r.ImageID, &r.NodeID, &r.Version, &r.State, &r.CanaryGateway, &r.HealthWindow, &r.SoakMS, &r.CreatedAt, &r.StartedAt, &r.FinishedAt, &r.FailureReason)
	return &r, err
}

// CreateRollout plans a rollout of image `imageID` over every node of the image's role: online nodes get real steps
// (canary first, then the rest by gateway id); nodes that are not reporting are listed as skipped, never updated blind.
func (p *PG) CreateRollout(ctx context.Context, imageID int32, canaryGW *int16, healthWindowMS, soakMS int32, onlineWithin time.Duration) (int32, error) {
	tx, err := p.Pool.Begin(ctx)
	if err != nil {
		return 0, err
	}
	defer tx.Rollback(ctx)
	var role int16
	var version int32
	if err := tx.QueryRow(ctx, `SELECT node_id, version FROM firmware_images WHERE id=$1`, imageID).Scan(&role, &version); err != nil {
		return 0, fmt.Errorf("image %d: %w", imageID, err)
	}
	var open int
	tx.QueryRow(ctx, `SELECT count(*) FROM rollouts r JOIN firmware_images i ON i.id=r.image_id WHERE r.state IN ('pending','running') AND i.node_id=$1`, role).Scan(&open)
	if open > 0 {
		return 0, fmt.Errorf("a rollout for this node type is already in progress")
	}
	rows, err := tx.Query(ctx, `SELECT gateway_id, last_seen > now() - $2::interval, fw_version FROM nodes WHERE node_id=$1 ORDER BY gateway_id`, role, fmt.Sprintf("%d milliseconds", onlineWithin.Milliseconds()))
	if err != nil {
		return 0, err
	}
	type cand struct {
		gw     int16
		online bool
		fw     int32
	}
	var cands []cand
	for rows.Next() {
		var c cand
		if err := rows.Scan(&c.gw, &c.online, &c.fw); err != nil {
			rows.Close()
			return 0, err
		}
		cands = append(cands, c)
	}
	rows.Close()
	var online, offline []cand
	for _, c := range cands {
		if c.online {
			online = append(online, c)
		} else {
			offline = append(offline, c)
		}
	}
	if len(online) == 0 {
		return 0, fmt.Errorf("no online nodes of this type to update")
	}
	if canaryGW != nil {
		for i, c := range online {
			if c.gw == *canaryGW {
				online = append([]cand{c}, append(online[:i:i], online[i+1:]...)...)
				break
			}
		}
		if online[0].gw != *canaryGW {
			return 0, fmt.Errorf("requested canary gateway %d is not online", *canaryGW)
		}
	}
	var id int32
	if err := tx.QueryRow(ctx, `INSERT INTO rollouts(image_id, state, canary_gateway, health_window_ms, soak_ms, started_at) VALUES ($1,'running',$2,$3,$4,now()) RETURNING id`,
		imageID, canaryGW, healthWindowMS, soakMS).Scan(&id); err != nil {
		return 0, err
	}
	ord := 0
	for i, c := range online {
		phase := "fleet"
		if i == 0 {
			phase = "canary"
		}
		if _, err := tx.Exec(ctx, `INSERT INTO rollout_steps(rollout_id, ordinal, gateway_id, node_id, phase, state) VALUES ($1,$2,$3,$4,$5,'pending')`, id, ord, c.gw, role, phase); err != nil {
			return 0, err
		}
		ord++
	}
	for _, c := range offline {
		if _, err := tx.Exec(ctx, `INSERT INTO rollout_steps(rollout_id, ordinal, gateway_id, node_id, phase, state, detail) VALUES ($1,$2,$3,$4,'fleet','skipped','not reporting when the rollout was planned')`, id, ord, c.gw, role); err != nil {
			return 0, err
		}
		ord++
	}
	if err := tx.Commit(ctx); err != nil {
		return 0, err
	}
	p.Notify(ctx, fmt.Sprintf("rollout:%d", id))
	return id, nil
}

func (p *PG) ListRollouts(ctx context.Context, limit int) ([]Rollout, error) {
	rows, err := p.Pool.Query(ctx, `SELECT `+rolloutCols+` FROM rollouts r JOIN firmware_images i ON i.id=r.image_id ORDER BY r.id DESC LIMIT $1`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Rollout
	for rows.Next() {
		r, err := scanRollout(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, *r)
	}
	return out, rows.Err()
}

func (p *PG) RunningRollouts(ctx context.Context) ([]Rollout, error) {
	rows, err := p.Pool.Query(ctx, `SELECT `+rolloutCols+` FROM rollouts r JOIN firmware_images i ON i.id=r.image_id WHERE r.state IN ('pending','running') ORDER BY r.id`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Rollout
	for rows.Next() {
		r, err := scanRollout(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, *r)
	}
	return out, rows.Err()
}

func (p *PG) GetRollout(ctx context.Context, id int32) (*Rollout, error) {
	r, err := scanRollout(p.Pool.QueryRow(ctx, `SELECT `+rolloutCols+` FROM rollouts r JOIN firmware_images i ON i.id=r.image_id WHERE r.id=$1`, id))
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	r.Steps, err = p.Steps(ctx, id)
	return r, err
}

func (p *PG) Steps(ctx context.Context, rolloutID int32) ([]Step, error) {
	rows, err := p.Pool.Query(ctx, `SELECT id, rollout_id, ordinal, gateway_id, node_id, phase, state, target_slot, prev_version, seen_target, started_at, transferred_at, healthy_since, updated_at, detail
		FROM rollout_steps WHERE rollout_id=$1 ORDER BY ordinal`, rolloutID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Step
	for rows.Next() {
		var s Step
		if err := rows.Scan(&s.ID, &s.RolloutID, &s.Ordinal, &s.GatewayID, &s.NodeID, &s.Phase, &s.State, &s.TargetSlot, &s.PrevVersion, &s.SeenTarget, &s.StartedAt, &s.TransferredAt, &s.HealthySince, &s.UpdatedAt, &s.Detail); err != nil {
			return nil, err
		}
		out = append(out, s)
	}
	return out, rows.Err()
}

// StepUpdate carries only the fields that change. ClearHealthy resets the soak timer.
type StepUpdate struct {
	State         string
	TargetSlot    *int16
	PrevVersion   *int32
	SeenTarget    *bool
	StartedAt     *time.Time
	TransferredAt *time.Time
	HealthySince  *time.Time
	ClearHealthy  bool
	Detail        *string
}

func (p *PG) UpdateStep(ctx context.Context, id int32, u StepUpdate) error {
	_, err := p.Pool.Exec(ctx, `UPDATE rollout_steps SET
		state = COALESCE(NULLIF($2,''), state),
		target_slot = COALESCE($3, target_slot),
		prev_version = COALESCE($4, prev_version),
		seen_target = COALESCE($5, seen_target),
		started_at = COALESCE($6, started_at),
		transferred_at = COALESCE($7, transferred_at),
		healthy_since = CASE WHEN $9 THEN NULL ELSE COALESCE($8, healthy_since) END,
		detail = COALESCE($10, detail),
		updated_at = now()
		WHERE id=$1`, id, u.State, u.TargetSlot, u.PrevVersion, u.SeenTarget, u.StartedAt, u.TransferredAt, u.HealthySince, u.ClearHealthy, u.Detail)
	if err == nil {
		p.Notify(ctx, "rollout")
	}
	return err
}

func (p *PG) FinishRollout(ctx context.Context, id int32, state, reason string) error {
	_, err := p.Pool.Exec(ctx, `UPDATE rollouts SET state=$2, finished_at=now(), failure_reason=NULLIF($3,'') WHERE id=$1 AND state IN ('pending','running')`, id, state, reason)
	if err == nil {
		p.Notify(ctx, "rollout")
	}
	return err
}

func (p *PG) SkipPendingSteps(ctx context.Context, rolloutID int32, why string) error {
	_, err := p.Pool.Exec(ctx, `UPDATE rollout_steps SET state='skipped', detail=$2, updated_at=now() WHERE rollout_id=$1 AND state='pending'`, rolloutID, why)
	return err
}

// ---- OTA transfers ------------------------------------------------------------------------------------------

func (p *PG) CreateTransfer(ctx context.Context, stepID int32, gw, node, slot int16, version int32, size int32) (int32, error) {
	var id int32
	err := p.Pool.QueryRow(ctx, `INSERT INTO ota_transfers(step_id, gateway_id, node_id, slot, version, state, size) VALUES ($1,$2,$3,$4,$5,'queued',$6) RETURNING id`,
		stepID, gw, node, slot, version, size).Scan(&id)
	return id, err
}

const transferCols = `id, step_id, gateway_id, node_id, slot, version, state, size, next_offset, detail, updated_at`

func scanTransfer(row pgx.Row) (*Transfer, error) {
	var t Transfer
	err := row.Scan(&t.ID, &t.StepID, &t.GatewayID, &t.NodeID, &t.Slot, &t.Version, &t.State, &t.Size, &t.NextOffset, &t.Detail, &t.UpdatedAt)
	return &t, err
}

func (p *PG) TransferForStep(ctx context.Context, stepID int32) (*Transfer, error) {
	t, err := scanTransfer(p.Pool.QueryRow(ctx, `SELECT `+transferCols+` FROM ota_transfers WHERE step_id=$1 ORDER BY id DESC LIMIT 1`, stepID))
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, nil
	}
	return t, err
}

func (p *PG) ActiveTransfers(ctx context.Context) ([]Transfer, error) {
	rows, err := p.Pool.Query(ctx, `SELECT `+transferCols+` FROM ota_transfers WHERE state IN ('queued','active') ORDER BY id`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Transfer
	for rows.Next() {
		t, err := scanTransfer(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, *t)
	}
	return out, rows.Err()
}

func (p *PG) SetTransferState(ctx context.Context, id int32, state, detail string) error {
	_, err := p.Pool.Exec(ctx, `UPDATE ota_transfers SET state=$2, detail=CASE WHEN $3='' THEN detail ELSE $3 END, updated_at=now() WHERE id=$1`, id, state, detail)
	if err == nil {
		p.Notify(ctx, "ota")
	}
	return err
}

// TransferBlob returns the signed image bytes for a transfer's slot.
func (p *PG) TransferBlob(ctx context.Context, transferID int32) ([]byte, error) {
	var blob []byte
	err := p.Pool.QueryRow(ctx, `SELECT CASE WHEN t.slot=0 THEN i.blob_a ELSE i.blob_b END FROM ota_transfers t
		JOIN rollout_steps s ON s.id=t.step_id JOIN rollouts r ON r.id=s.rollout_id JOIN firmware_images i ON i.id=r.image_id WHERE t.id=$1`, transferID).Scan(&blob)
	return blob, err
}

// ImageBlobForSlot is used by the rollout engine to size a transfer before creating it.
func (p *PG) ImageBlobLen(ctx context.Context, imageID int32, slot int16) (int32, error) {
	var n int32
	err := p.Pool.QueryRow(ctx, `SELECT length(CASE WHEN $2=0 THEN blob_a ELSE blob_b END) FROM firmware_images WHERE id=$1`, imageID, slot).Scan(&n)
	return n, err
}

// CancelledTransfers lists transfers cancelled recently, so the OTA driver can tell the gateway to drop them.
func (p *PG) CancelledTransfers(ctx context.Context, within time.Duration) ([]Transfer, error) {
	rows, err := p.Pool.Query(ctx, `SELECT `+transferCols+` FROM ota_transfers WHERE state='cancelled' AND updated_at > now() - $1::interval`, fmt.Sprintf("%d milliseconds", within.Milliseconds()))
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []Transfer
	for rows.Next() {
		t, err := scanTransfer(rows)
		if err != nil {
			return nil, err
		}
		out = append(out, *t)
	}
	return out, rows.Err()
}

// AbortRollout stops a rollout: pending steps are skipped and any transfer in flight is cancelled.
// A step already past the point of no return (verifying) is left to finish its health check; the rollout just stops
// promoting.
func (p *PG) AbortRollout(ctx context.Context, id int32, why string) error {
	tx, err := p.Pool.Begin(ctx)
	if err != nil {
		return err
	}
	defer tx.Rollback(ctx)
	tag, err := tx.Exec(ctx, `UPDATE rollouts SET state='aborted', finished_at=now(), failure_reason=$2 WHERE id=$1 AND state IN ('pending','running')`, id, why)
	if err != nil {
		return err
	}
	if tag.RowsAffected() == 0 {
		return fmt.Errorf("rollout %d is not running", id)
	}
	tx.Exec(ctx, `UPDATE rollout_steps SET state='skipped', detail='rollout aborted', updated_at=now() WHERE rollout_id=$1 AND state IN ('pending','transferring')`, id)
	tx.Exec(ctx, `UPDATE ota_transfers SET state='cancelled', detail='rollout aborted', updated_at=now() WHERE state IN ('queued','active') AND step_id IN (SELECT id FROM rollout_steps WHERE rollout_id=$1)`, id)
	if err := tx.Commit(ctx); err != nil {
		return err
	}
	p.Notify(ctx, "rollout")
	return nil
}
