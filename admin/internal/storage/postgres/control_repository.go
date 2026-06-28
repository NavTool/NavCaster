package postgres

import (
	"context"
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"strings"
	"time"

	"navcaster-admin/internal/control"
	"navcaster-admin/internal/errorsx"
)

type ControlRepository struct {
	db *sql.DB
}

func NewControlRepository(db *sql.DB) *ControlRepository {
	return &ControlRepository{db: db}
}

func (r *ControlRepository) ControlPlaneStatus() (control.ControlPlaneStatus, error) {
	status := control.ControlPlaneStatus{
		Status:     "ok",
		Repository: "postgres",
	}
	ctx := context.Background()
	row := r.db.QueryRowContext(ctx, `
SELECT
  (SELECT count(*) FROM hosts),
  (SELECT count(*) FROM runtimes),
  (SELECT count(*) FROM runtime_desired_states),
  COALESCE((SELECT max(version) FROM runtime_desired_states), 0),
  (SELECT count(*) FROM control_intents WHERE status IN ('accepted', 'projected')),
  (SELECT count(*) FROM control_intents WHERE status = 'failed'),
  now()`)
	var updatedAt time.Time
	if err := row.Scan(
		&status.HostCount,
		&status.RuntimeCount,
		&status.DesiredCount,
		&status.LatestDesiredVersion,
		&status.PendingIntentCount,
		&status.FailedIntentCount,
		&updatedAt,
	); err != nil {
		return control.ControlPlaneStatus{}, err
	}
	status.UpdatedAt = &updatedAt

	runtimes, err := r.ListRuntimes()
	if err != nil {
		return control.ControlPlaneStatus{}, err
	}
	for _, runtime := range runtimes {
		if runtime.Control != nil && runtime.Control.Stale {
			status.StaleRuntimeCount++
		}
	}
	return status, nil
}

func (r *ControlRepository) ListHosts() ([]control.Host, error) {
	rows, err := r.db.QueryContext(context.Background(), `
SELECT h.host_id, h.display_name, COALESCE(a.agent_id, ''), h.status, h.labels::text,
       h.last_heartbeat_at, h.created_at, h.updated_at
FROM hosts h
LEFT JOIN agents a ON a.host_id = h.host_id
ORDER BY h.host_id`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var hosts []control.Host
	for rows.Next() {
		host, err := scanHost(rows)
		if err != nil {
			return nil, err
		}
		hosts = append(hosts, host)
	}
	return hosts, rows.Err()
}

func (r *ControlRepository) GetHost(hostID string) (control.Host, error) {
	row := r.db.QueryRowContext(context.Background(), `
SELECT h.host_id, h.display_name, COALESCE(a.agent_id, ''), h.status, h.labels::text,
       h.last_heartbeat_at, h.created_at, h.updated_at
FROM hosts h
LEFT JOIN agents a ON a.host_id = h.host_id
WHERE h.host_id = $1`, hostID)
	host, err := scanHost(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return control.Host{}, errorsx.NotFound("host not found")
		}
		return control.Host{}, err
	}
	return host, nil
}

func (r *ControlRepository) UpsertHost(host control.Host) (control.Host, error) {
	if host.ID == "" {
		host.ID = "host_" + randomHex(8)
	}
	if host.DisplayName == "" {
		host.DisplayName = host.ID
	}
	if host.Status == "" {
		host.Status = "registered"
	}
	labels := host.Labels
	if labels == nil {
		labels = map[string]any{}
	}
	labelsJSON, err := json.Marshal(labels)
	if err != nil {
		return control.Host{}, errorsx.BadRequest("labels must be JSON serializable")
	}
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return control.Host{}, err
	}
	defer rollback(tx)

	if _, err := tx.ExecContext(ctx, `
INSERT INTO hosts (host_id, display_name, status, labels, created_at, updated_at)
VALUES ($1, $2, $3, $4::jsonb, now(), now())
ON CONFLICT (host_id) DO UPDATE SET
  display_name = EXCLUDED.display_name,
  status = EXCLUDED.status,
  labels = EXCLUDED.labels,
  updated_at = now()`,
		host.ID, host.DisplayName, host.Status, string(labelsJSON)); err != nil {
		return control.Host{}, err
	}
	if host.AgentID != "" {
		if _, err := tx.ExecContext(ctx, `DELETE FROM agents WHERE host_id = $1 AND agent_id <> $2`, host.ID, host.AgentID); err != nil {
			return control.Host{}, err
		}
		if _, err := tx.ExecContext(ctx, `
INSERT INTO agents (agent_id, host_id, agent_secret_hash, status, registered_at, updated_at)
VALUES ($1, $2, '', $3, now(), now())
ON CONFLICT (agent_id) DO UPDATE SET
  host_id = EXCLUDED.host_id,
  status = EXCLUDED.status,
  updated_at = now()`,
			host.AgentID, host.ID, host.Status); err != nil {
			return control.Host{}, err
		}
	}
	if err := tx.Commit(); err != nil {
		return control.Host{}, err
	}
	return r.GetHost(host.ID)
}

func (r *ControlRepository) ListRuntimes() ([]control.Runtime, error) {
	rows, err := r.db.QueryContext(context.Background(), runtimeSelectSQL()+` ORDER BY rt.runtime_id`)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var runtimes []control.Runtime
	for rows.Next() {
		runtime, err := scanRuntime(rows)
		if err != nil {
			return nil, err
		}
		runtimes = append(runtimes, r.withControl(runtime))
	}
	return runtimes, rows.Err()
}

func (r *ControlRepository) GetRuntime(runtimeID string) (control.Runtime, error) {
	row := r.db.QueryRowContext(context.Background(), runtimeSelectSQL()+` WHERE rt.runtime_id = $1`, runtimeID)
	runtime, err := scanRuntime(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return control.Runtime{}, errorsx.NotFound("runtime not found")
		}
		return control.Runtime{}, err
	}
	return r.withControl(runtime), nil
}

func (r *ControlRepository) CreateRuntime(req control.RuntimeCreateRequest) (control.Runtime, error) {
	if req.HostID == "" {
		return control.Runtime{}, errorsx.BadRequest("host_id is required")
	}
	if req.DesiredState == "" {
		if req.StartImmediately {
			req.DesiredState = control.DesiredStateRunning
		} else {
			req.DesiredState = control.DesiredStateStopped
		}
	}
	if err := validateDesired(req.DesiredState, req.ListenPort, req.WorkerCount, req.MaxWorkers, req.RestartPolicy); err != nil {
		return control.Runtime{}, err
	}
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return control.Runtime{}, err
	}
	defer rollback(tx)

	if err := ensureHostExistsTx(ctx, tx, req.HostID); err != nil {
		return control.Runtime{}, err
	}
	runtimeID := "rt_" + randomHex(8)
	name := firstNonEmpty(req.Name, runtimeID)
	version, err := nextDesiredVersion(ctx, tx)
	if err != nil {
		return control.Runtime{}, err
	}
	if _, err := tx.ExecContext(ctx, `
INSERT INTO runtimes (runtime_id, host_id, name, status, created_at, updated_at)
VALUES ($1, $2, $3, 'created', now(), now())`, runtimeID, req.HostID, name); err != nil {
		return control.Runtime{}, err
	}
	if _, err := tx.ExecContext(ctx, `
INSERT INTO runtime_desired_states
  (runtime_id, host_id, desired_state, config_version, listen_port, worker_count,
   max_worker_count, restart_policy, draining, action_intent, version, generation, updated_at)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, '{}'::jsonb, $10, $11, now())`,
		runtimeID, req.HostID, string(req.DesiredState), req.ConfigVersion, req.ListenPort,
		req.WorkerCount, req.MaxWorkers, string(req.RestartPolicy),
		req.DesiredState == control.DesiredStateDraining, version, version); err != nil {
		return control.Runtime{}, err
	}
	if err := insertAuditTx(ctx, tx, "runtime.create", "runtime", runtimeID, map[string]any{"desired_version": version}); err != nil {
		return control.Runtime{}, err
	}
	if err := tx.Commit(); err != nil {
		return control.Runtime{}, err
	}
	return r.GetRuntime(runtimeID)
}

func (r *ControlRepository) UpdateDesiredState(runtimeID string, req control.DesiredUpdateRequest) (control.Runtime, error) {
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return control.Runtime{}, err
	}
	defer rollback(tx)
	runtime, err := getRuntimeTx(ctx, tx, runtimeID)
	if err != nil {
		return control.Runtime{}, err
	}
	if runtime.Desired == nil {
		return control.Runtime{}, errorsx.Conflict("runtime has no desired state")
	}
	desired := *runtime.Desired
	if req.DesiredState != "" {
		desired.DesiredState = req.DesiredState
	}
	if req.ConfigVersion != nil {
		desired.ConfigVersion = *req.ConfigVersion
	}
	if req.ListenPort != nil {
		desired.ListenPort = *req.ListenPort
	}
	if req.WorkerCount != nil {
		desired.WorkerCount = *req.WorkerCount
	}
	if req.MaxWorkers != nil {
		desired.MaxWorkers = *req.MaxWorkers
	}
	if req.RestartPolicy != "" {
		desired.RestartPolicy = req.RestartPolicy
	}
	if req.Draining != nil {
		desired.Draining = *req.Draining
	}
	if desired.DesiredState == control.DesiredStateDraining {
		desired.Draining = true
	}
	if err := validateDesired(desired.DesiredState, desired.ListenPort, desired.WorkerCount, desired.MaxWorkers, desired.RestartPolicy); err != nil {
		return control.Runtime{}, err
	}
	version, err := nextDesiredVersion(ctx, tx)
	if err != nil {
		return control.Runtime{}, err
	}
	if err := updateDesiredTx(ctx, tx, desired, version, map[string]any{"kind": "desired_state_update"}); err != nil {
		return control.Runtime{}, err
	}
	if err := insertAuditTx(ctx, tx, "runtime.desired_state.update", "runtime", runtimeID, map[string]any{"desired_version": version}); err != nil {
		return control.Runtime{}, err
	}
	if err := tx.Commit(); err != nil {
		return control.Runtime{}, err
	}
	return r.GetRuntime(runtimeID)
}

func (r *ControlRepository) RecordActionIntent(runtimeID string, kind control.ActionKind, req control.ActionRequest) (control.ActionIntent, control.Runtime, error) {
	if kind == "" {
		return control.ActionIntent{}, control.Runtime{}, errorsx.BadRequest("action kind is required")
	}
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	defer rollback(tx)
	if req.Payload == nil {
		req.Payload = map[string]any{}
	}
	if req.RequestID != "" {
		existing, err := getIntentByRequestIDTx(ctx, tx, req.RequestID)
		if err == nil {
			if err := tx.Commit(); err != nil {
				return control.ActionIntent{}, control.Runtime{}, err
			}
			runtime, runtimeErr := r.GetRuntime(existing.RuntimeID)
			if runtimeErr != nil {
				return control.ActionIntent{}, control.Runtime{}, runtimeErr
			}
			return existing, runtime, nil
		}
		if err != sql.ErrNoRows {
			return control.ActionIntent{}, control.Runtime{}, err
		}
	}
	runtime, err := getRuntimeTx(ctx, tx, runtimeID)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	if runtime.Desired == nil {
		return control.ActionIntent{}, control.Runtime{}, errorsx.Conflict("runtime has no desired state")
	}
	desired := *runtime.Desired
	switch kind {
	case control.ActionKindStart:
		desired.DesiredState = control.DesiredStateRunning
		desired.Draining = false
	case control.ActionKindStop:
		desired.DesiredState = control.DesiredStateStopped
		desired.Draining = false
	case control.ActionKindDrain:
		desired.DesiredState = control.DesiredStateDraining
		desired.Draining = true
	case control.ActionKindUndrain:
		desired.DesiredState = control.DesiredStateRunning
		desired.Draining = false
	case control.ActionKindRestart:
		desired.DesiredState = control.DesiredStateRunning
	default:
		return control.ActionIntent{}, control.Runtime{}, errorsx.BadRequest("unsupported action kind")
	}
	if err := validateDesired(desired.DesiredState, desired.ListenPort, desired.WorkerCount, desired.MaxWorkers, desired.RestartPolicy); err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	version, err := nextDesiredVersion(ctx, tx)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	intentID, err := nextIntentID(ctx, tx)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	requestID := firstNonEmpty(req.RequestID, intentID)
	if err := supersedeOpenIntentsTx(ctx, tx, runtimeID); err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	payloadJSON, err := json.Marshal(req.Payload)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, errorsx.BadRequest("action payload must be JSON serializable")
	}
	if err := updateDesiredTx(ctx, tx, desired, version, map[string]any{"kind": string(kind), "intent_id": intentID, "request_id": requestID, "payload": req.Payload}); err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	if _, err := tx.ExecContext(ctx, `
INSERT INTO control_intents
  (intent_id, request_id, runtime_id, host_id, kind, status, desired_version, payload, created_at, updated_at)
VALUES ($1, $2, $3, $4, $5, 'accepted', $6, $7::jsonb, now(), now())`,
		intentID, requestID, runtimeID, runtime.HostID, string(kind), version, string(payloadJSON)); err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	if err := insertAuditTx(ctx, tx, "runtime.action."+string(kind), "runtime", runtimeID, map[string]any{"intent_id": intentID, "desired_version": version}); err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	if err := tx.Commit(); err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	updated, err := r.GetRuntime(runtimeID)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	intents, err := r.ListActionIntents(runtimeID, 1)
	if err != nil {
		return control.ActionIntent{}, control.Runtime{}, err
	}
	intent := control.ActionIntent{}
	if len(intents) > 0 {
		intent = intents[0]
	}
	return intent, updated, nil
}

func (r *ControlRepository) UpdateActionIntentStatus(intentID string, status control.ActionIntentStatus, reason string) error {
	ctx := context.Background()
	result, err := r.db.ExecContext(ctx, `
UPDATE control_intents
SET status = $2,
    updated_at = now(),
    projected_at = CASE WHEN $2 = 'projected' THEN now() ELSE projected_at END,
    observed_at = CASE WHEN $2 = 'observed' THEN now() ELSE observed_at END,
    superseded_at = CASE WHEN $2 = 'superseded' THEN now() ELSE superseded_at END,
    failed_at = CASE WHEN $2 = 'failed' THEN now() ELSE failed_at END,
    failure_reason = CASE WHEN $2 = 'failed' THEN $3 ELSE failure_reason END
WHERE intent_id = $1`,
		intentID, string(status), reason)
	if err != nil {
		return err
	}
	rows, err := result.RowsAffected()
	if err != nil {
		return err
	}
	if rows == 0 {
		return errorsx.NotFound("intent not found")
	}
	return nil
}

func (r *ControlRepository) ListActionIntents(runtimeID string, limit int) ([]control.ActionIntent, error) {
	if limit <= 0 || limit > 200 {
		limit = 50
	}
	ctx := context.Background()
	query := `
SELECT intent_id, request_id, runtime_id, COALESCE(host_id, ''), kind, status, desired_version,
       payload::text, created_at, updated_at, projected_at, observed_at, superseded_at, failed_at, failure_reason
FROM control_intents`
	args := []any{}
	if runtimeID != "" {
		query += ` WHERE runtime_id = $1`
		args = append(args, runtimeID)
	}
	query += fmt.Sprintf(` ORDER BY created_at DESC, intent_id DESC LIMIT %d`, limit)
	rows, err := r.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var intents []control.ActionIntent
	for rows.Next() {
		intent, err := scanActionIntent(rows)
		if err != nil {
			return nil, err
		}
		intents = append(intents, intent)
	}
	return intents, rows.Err()
}

func (r *ControlRepository) ListRuntimeEvents(runtimeID string, limit int) ([]control.RuntimeEvent, error) {
	if limit <= 0 || limit > 500 {
		limit = 100
	}
	ctx := context.Background()
	query := `
SELECT event_id, runtime_id, host_id, COALESCE(agent_id, ''), type, severity, desired_version,
       process_id, message, metadata::text, occurred_at
FROM runtime_events`
	args := []any{}
	if runtimeID != "" {
		query += ` WHERE runtime_id = $1`
		args = append(args, runtimeID)
	}
	query += fmt.Sprintf(` ORDER BY occurred_at DESC, event_id DESC LIMIT %d`, limit)
	rows, err := r.db.QueryContext(ctx, query, args...)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var events []control.RuntimeEvent
	for rows.Next() {
		event, err := scanRuntimeEvent(rows)
		if err != nil {
			return nil, err
		}
		events = append(events, event)
	}
	return events, rows.Err()
}

func (r *ControlRepository) ListDesiredStatesForHost(hostID string, sinceVersion int64) ([]control.DesiredRuntime, error) {
	rows, err := r.db.QueryContext(context.Background(), `
SELECT runtime_id, host_id, desired_state, config_version, listen_port, worker_count,
       max_worker_count, restart_policy, draining, version, generation, updated_at
FROM runtime_desired_states
WHERE host_id = $1 AND version > $2
ORDER BY version, runtime_id`, hostID, sinceVersion)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var states []control.DesiredRuntime
	for rows.Next() {
		state, err := scanDesired(rows)
		if err != nil {
			return nil, err
		}
		states = append(states, state)
	}
	return states, rows.Err()
}

func (r *ControlRepository) ApplyHeartbeat(hostID string, agentID string, at time.Time) error {
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer rollback(tx)
	if _, err := tx.ExecContext(ctx, `
INSERT INTO hosts (host_id, display_name, status, labels, last_heartbeat_at, created_at, updated_at)
VALUES ($1, $1, 'online', '{}'::jsonb, $2, now(), $2)
ON CONFLICT (host_id) DO UPDATE SET
  status = 'online',
  last_heartbeat_at = EXCLUDED.last_heartbeat_at,
  updated_at = EXCLUDED.updated_at`, hostID, at); err != nil {
		return err
	}
	if _, err := tx.ExecContext(ctx, `
INSERT INTO agents (agent_id, host_id, agent_secret_hash, status, last_heartbeat_at, registered_at, updated_at)
VALUES ($1, $2, '', 'online', $3, now(), $3)
ON CONFLICT (agent_id) DO UPDATE SET
  host_id = EXCLUDED.host_id,
  status = 'online',
  last_heartbeat_at = EXCLUDED.last_heartbeat_at,
  updated_at = EXCLUDED.updated_at`, agentID, hostID, at); err != nil {
		return err
	}
	return tx.Commit()
}

func (r *ControlRepository) ApplyActualSnapshots(agentID string, hostID string, snapshots []control.ActualSnapshot) error {
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer rollback(tx)
	now := time.Now().UTC()
	if err := ensureHostOnlineTx(ctx, tx, hostID, agentID, now); err != nil {
		return err
	}
	for _, snapshot := range snapshots {
		if snapshot.RuntimeID == "" {
			continue
		}
		if err := validateIngestIdentity(snapshot.HostID, snapshot.AgentID, hostID, agentID); err != nil {
			return err
		}
		snapshot.HostID = hostID
		snapshot.AgentID = agentID
		if snapshot.UpdatedAt.IsZero() {
			snapshot.UpdatedAt = now
		}
		if err := upsertObservedRuntimeTx(ctx, tx, snapshot.RuntimeID, snapshot.HostID); err != nil {
			return err
		}
		if _, err := tx.ExecContext(ctx, `
INSERT INTO runtime_actual_snapshots
  (runtime_id, host_id, agent_id, actual_state, process_id, start_token, config_version,
   config_path, config_checksum, listen_port, worker_count, connections, mounts, sources, clients,
   send_bps, recv_bps, loop_delay_ms_p95, redis_connected, observed_desired_version,
   last_error, started_at, last_exit_code, payload, observed_at)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15,
        $16, $17, $18, $19, $20, $21, $22, $23, '{}'::jsonb, $24)`,
			snapshot.RuntimeID, snapshot.HostID, nullableString(snapshot.AgentID), snapshot.ActualState,
			nullableInt(snapshot.ProcessID), nullableString(snapshot.StartToken), nullableInt64(snapshot.ConfigVersion),
			snapshot.ConfigPath, snapshot.ConfigChecksum, nullableInt(snapshot.ListenPort), nullableInt(snapshot.WorkerCount),
			snapshot.Connections, snapshot.Mounts, snapshot.Sources, snapshot.Clients,
			snapshot.SendBPS, snapshot.RecvBPS, snapshot.LoopDelayP95MS, snapshot.RedisConnected,
			snapshot.ObservedDesiredVersion, snapshot.LastError, nullableTime(snapshot.StartedAt),
			nullableIntPtr(snapshot.LastExitCode), snapshot.UpdatedAt); err != nil {
			return err
		}
		if err := observeIntentFromActualTx(ctx, tx, snapshot); err != nil {
			return err
		}
	}
	return tx.Commit()
}

func (r *ControlRepository) RecordRuntimeEvents(agentID string, hostID string, events []control.RuntimeEvent) error {
	ctx := context.Background()
	tx, err := r.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer rollback(tx)
	now := time.Now().UTC()
	if err := ensureHostOnlineTx(ctx, tx, hostID, agentID, now); err != nil {
		return err
	}
	for _, event := range events {
		if event.RuntimeID == "" || event.Type == "" {
			continue
		}
		if err := validateIngestIdentity(event.HostID, event.AgentID, hostID, agentID); err != nil {
			return err
		}
		event.HostID = hostID
		event.AgentID = agentID
		if event.OccurredAt.IsZero() {
			event.OccurredAt = now
		}
		if event.EventID == "" {
			event.EventID = fmt.Sprintf("rtevt_%s_%s", event.OccurredAt.Format("20060102150405"), randomHex(6))
		}
		if err := upsertObservedRuntimeTx(ctx, tx, event.RuntimeID, event.HostID); err != nil {
			return err
		}
		metadataJSON, err := json.Marshal(event.Metadata)
		if err != nil {
			return errorsx.BadRequest("event metadata must be JSON serializable")
		}
		if _, err := tx.ExecContext(ctx, `
INSERT INTO runtime_events
  (event_id, runtime_id, host_id, agent_id, type, severity, desired_version,
   process_id, message, metadata, occurred_at, ingested_at)
VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10::jsonb, $11, now())
ON CONFLICT (event_id) DO NOTHING`,
			event.EventID, event.RuntimeID, event.HostID, nullableString(event.AgentID), event.Type,
			event.Severity, event.DesiredVersion, nullableInt(event.ProcessID), event.Message,
			string(metadataJSON), event.OccurredAt); err != nil {
			return err
		}
		if err := observeIntentFromEventTx(ctx, tx, event); err != nil {
			return err
		}
	}
	return tx.Commit()
}

func validateIngestIdentity(payloadHostID string, payloadAgentID string, requestHostID string, requestAgentID string) error {
	if payloadHostID != "" && payloadHostID != requestHostID {
		return errorsx.BadRequest("payload host_id must match request host_id")
	}
	if payloadAgentID != "" && payloadAgentID != requestAgentID {
		return errorsx.BadRequest("payload agent_id must match request agent_id")
	}
	return nil
}

type scanner interface {
	Scan(dest ...any) error
}

func scanHost(row scanner) (control.Host, error) {
	var host control.Host
	var labelsRaw string
	var lastHeartbeat sql.NullTime
	if err := row.Scan(&host.ID, &host.DisplayName, &host.AgentID, &host.Status, &labelsRaw, &lastHeartbeat, &host.CreatedAt, &host.UpdatedAt); err != nil {
		return control.Host{}, err
	}
	if labelsRaw != "" {
		var labels map[string]any
		if err := json.Unmarshal([]byte(labelsRaw), &labels); err == nil {
			host.Labels = labels
		}
	}
	if lastHeartbeat.Valid {
		host.LastHeartbeat = &lastHeartbeat.Time
	}
	return host, nil
}

func runtimeSelectSQL() string {
	return `
SELECT rt.runtime_id, rt.host_id, rt.name, rt.created_at, rt.updated_at,
       ds.runtime_id, ds.host_id, ds.desired_state, ds.config_version, ds.listen_port,
       ds.worker_count, ds.max_worker_count, ds.restart_policy, ds.draining, ds.version,
       ds.generation, ds.updated_at,
       act.runtime_id, act.host_id, COALESCE(act.agent_id, ''), act.actual_state,
       act.process_id, act.start_token, act.config_version, act.config_path, act.config_checksum,
       act.listen_port, act.worker_count, act.connections, act.mounts, act.sources, act.clients,
       act.send_bps, act.recv_bps, act.loop_delay_ms_p95, act.redis_connected,
       act.observed_desired_version, act.last_error, act.started_at, act.observed_at, act.last_exit_code
FROM runtimes rt
LEFT JOIN runtime_desired_states ds ON ds.runtime_id = rt.runtime_id
LEFT JOIN LATERAL (
  SELECT *
  FROM runtime_actual_snapshots ras
  WHERE ras.runtime_id = rt.runtime_id
  ORDER BY ras.observed_at DESC, ras.snapshot_id DESC
  LIMIT 1
) act ON true`
}

func scanRuntime(row scanner) (control.Runtime, error) {
	var runtime control.Runtime
	var desiredRuntimeID, desiredHostID, desiredState, restartPolicy sql.NullString
	var desiredConfigVersion, desiredVersion, desiredGeneration sql.NullInt64
	var desiredListenPort, desiredWorkerCount, desiredMaxWorkers sql.NullInt64
	var desiredDraining sql.NullBool
	var desiredUpdatedAt sql.NullTime

	var actualRuntimeID, actualHostID, actualAgentID, actualState sql.NullString
	var actualProcessID, actualListenPort, actualWorkerCount, connections, mounts, sources, clients sql.NullInt64
	var startToken, configPath, configChecksum, lastError sql.NullString
	var actualConfigVersion, sendBPS, recvBPS, observedDesiredVersion sql.NullInt64
	var loopDelay sql.NullInt64
	var redisConnected sql.NullBool
	var startedAt, actualUpdatedAt sql.NullTime
	var lastExitCode sql.NullInt64

	err := row.Scan(
		&runtime.RuntimeID, &runtime.HostID, &runtime.Name, &runtime.CreatedAt, &runtime.UpdatedAt,
		&desiredRuntimeID, &desiredHostID, &desiredState, &desiredConfigVersion, &desiredListenPort,
		&desiredWorkerCount, &desiredMaxWorkers, &restartPolicy, &desiredDraining, &desiredVersion,
		&desiredGeneration, &desiredUpdatedAt,
		&actualRuntimeID, &actualHostID, &actualAgentID, &actualState, &actualProcessID,
		&startToken, &actualConfigVersion, &configPath, &configChecksum, &actualListenPort,
		&actualWorkerCount, &connections, &mounts, &sources, &clients, &sendBPS, &recvBPS,
		&loopDelay, &redisConnected, &observedDesiredVersion, &lastError, &startedAt,
		&actualUpdatedAt, &lastExitCode,
	)
	if err != nil {
		return control.Runtime{}, err
	}
	if desiredRuntimeID.Valid {
		runtime.Desired = &control.DesiredRuntime{
			RuntimeID:     desiredRuntimeID.String,
			HostID:        desiredHostID.String,
			DesiredState:  control.DesiredState(desiredState.String),
			ConfigVersion: desiredConfigVersion.Int64,
			ListenPort:    int(desiredListenPort.Int64),
			WorkerCount:   int(desiredWorkerCount.Int64),
			MaxWorkers:    int(desiredMaxWorkers.Int64),
			RestartPolicy: control.RestartPolicy(restartPolicy.String),
			Draining:      desiredDraining.Bool,
			Version:       desiredVersion.Int64,
			Generation:    desiredGeneration.Int64,
			UpdatedAt:     desiredUpdatedAt.Time,
		}
	}
	if actualRuntimeID.Valid {
		actual := &control.ActualSnapshot{
			RuntimeID:              actualRuntimeID.String,
			HostID:                 actualHostID.String,
			AgentID:                actualAgentID.String,
			ActualState:            actualState.String,
			ProcessID:              int(actualProcessID.Int64),
			StartToken:             startToken.String,
			ConfigVersion:          actualConfigVersion.Int64,
			ConfigPath:             configPath.String,
			ConfigChecksum:         configChecksum.String,
			ListenPort:             int(actualListenPort.Int64),
			WorkerCount:            int(actualWorkerCount.Int64),
			Connections:            int(connections.Int64),
			Mounts:                 int(mounts.Int64),
			Sources:                int(sources.Int64),
			Clients:                int(clients.Int64),
			SendBPS:                sendBPS.Int64,
			RecvBPS:                recvBPS.Int64,
			LoopDelayP95MS:         int(loopDelay.Int64),
			RedisConnected:         redisConnected.Bool,
			LastError:              lastError.String,
			ObservedDesiredVersion: observedDesiredVersion.Int64,
			UpdatedAt:              actualUpdatedAt.Time,
		}
		if startedAt.Valid {
			actual.StartedAt = startedAt.Time
		}
		if lastExitCode.Valid {
			v := int(lastExitCode.Int64)
			actual.LastExitCode = &v
		}
		runtime.Actual = actual
	}
	return runtime, nil
}

func scanDesired(row scanner) (control.DesiredRuntime, error) {
	var state control.DesiredRuntime
	var desiredState, restartPolicy string
	err := row.Scan(
		&state.RuntimeID, &state.HostID, &desiredState, &state.ConfigVersion, &state.ListenPort,
		&state.WorkerCount, &state.MaxWorkers, &restartPolicy, &state.Draining,
		&state.Version, &state.Generation, &state.UpdatedAt,
	)
	state.DesiredState = control.DesiredState(desiredState)
	state.RestartPolicy = control.RestartPolicy(restartPolicy)
	return state, err
}

func scanActionIntent(row scanner) (control.ActionIntent, error) {
	var intent control.ActionIntent
	var kind, status string
	var payloadRaw string
	var projectedAt, observedAt, supersededAt, failedAt sql.NullTime
	err := row.Scan(
		&intent.ID, &intent.RequestID, &intent.RuntimeID, &intent.HostID, &kind, &status,
		&intent.DesiredVersion, &payloadRaw, &intent.CreatedAt, &intent.UpdatedAt,
		&projectedAt, &observedAt, &supersededAt, &failedAt, &intent.FailureReason,
	)
	if err != nil {
		return control.ActionIntent{}, err
	}
	intent.Kind = control.ActionKind(kind)
	intent.Status = control.ActionIntentStatus(status)
	if payloadRaw != "" {
		var payload map[string]any
		if err := json.Unmarshal([]byte(payloadRaw), &payload); err == nil {
			intent.Payload = payload
		}
	}
	if projectedAt.Valid {
		intent.ProjectedAt = &projectedAt.Time
	}
	if observedAt.Valid {
		intent.ObservedAt = &observedAt.Time
	}
	if supersededAt.Valid {
		intent.SupersededAt = &supersededAt.Time
	}
	if failedAt.Valid {
		intent.FailedAt = &failedAt.Time
	}
	return intent, nil
}

func scanRuntimeEvent(row scanner) (control.RuntimeEvent, error) {
	var event control.RuntimeEvent
	var metadataRaw string
	var processID sql.NullInt64
	err := row.Scan(
		&event.EventID, &event.RuntimeID, &event.HostID, &event.AgentID, &event.Type,
		&event.Severity, &event.DesiredVersion, &processID, &event.Message,
		&metadataRaw, &event.OccurredAt,
	)
	if err != nil {
		return control.RuntimeEvent{}, err
	}
	if processID.Valid {
		event.ProcessID = int(processID.Int64)
	}
	if metadataRaw != "" {
		var metadata map[string]string
		if err := json.Unmarshal([]byte(metadataRaw), &metadata); err == nil {
			event.Metadata = metadata
		}
	}
	return event, nil
}

func getRuntimeTx(ctx context.Context, tx *sql.Tx, runtimeID string) (control.Runtime, error) {
	row := tx.QueryRowContext(ctx, runtimeSelectSQL()+` WHERE rt.runtime_id = $1 FOR UPDATE OF rt`, runtimeID)
	runtime, err := scanRuntime(row)
	if err != nil {
		if err == sql.ErrNoRows {
			return control.Runtime{}, errorsx.NotFound("runtime not found")
		}
		return control.Runtime{}, err
	}
	return runtime, nil
}

func getIntentByRequestIDTx(ctx context.Context, tx *sql.Tx, requestID string) (control.ActionIntent, error) {
	row := tx.QueryRowContext(ctx, `
SELECT intent_id, request_id, runtime_id, COALESCE(host_id, ''), kind, status, desired_version,
       payload::text, created_at, updated_at, projected_at, observed_at, superseded_at, failed_at, failure_reason
FROM control_intents
WHERE request_id = $1`, requestID)
	return scanActionIntent(row)
}

func getLatestIntent(ctx context.Context, db *sql.DB, runtimeID string) (*control.ActionIntent, error) {
	row := db.QueryRowContext(ctx, `
SELECT intent_id, request_id, runtime_id, COALESCE(host_id, ''), kind, status, desired_version,
       payload::text, created_at, updated_at, projected_at, observed_at, superseded_at, failed_at, failure_reason
FROM control_intents
WHERE runtime_id = $1
ORDER BY created_at DESC, intent_id DESC
LIMIT 1`, runtimeID)
	intent, err := scanActionIntent(row)
	if err == sql.ErrNoRows {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &intent, nil
}

func supersedeOpenIntentsTx(ctx context.Context, tx *sql.Tx, runtimeID string) error {
	_, err := tx.ExecContext(ctx, `
UPDATE control_intents
SET status = 'superseded',
    superseded_at = now(),
    updated_at = now()
WHERE runtime_id = $1 AND status IN ('accepted', 'projected')`, runtimeID)
	return err
}

func observeIntentFromActualTx(ctx context.Context, tx *sql.Tx, snapshot control.ActualSnapshot) error {
	if snapshot.ObservedDesiredVersion == 0 {
		return nil
	}
	if snapshot.LastError != "" {
		_, err := tx.ExecContext(ctx, `
UPDATE control_intents
SET status = 'failed',
    updated_at = now(),
    failed_at = now(),
    failure_reason = $3
WHERE runtime_id = $1
  AND desired_version = $2
  AND status IN ('accepted', 'projected')`,
			snapshot.RuntimeID, snapshot.ObservedDesiredVersion, snapshot.LastError)
		return err
	}
	_, err := tx.ExecContext(ctx, `
UPDATE control_intents
SET status = 'observed',
    updated_at = now(),
    observed_at = now(),
    failed_at = NULL,
    failure_reason = ''
WHERE runtime_id = $1
  AND desired_version = $2
  AND status IN ('accepted', 'projected', 'failed')`,
		snapshot.RuntimeID, snapshot.ObservedDesiredVersion)
	return err
}

func observeIntentFromEventTx(ctx context.Context, tx *sql.Tx, event control.RuntimeEvent) error {
	if event.DesiredVersion == 0 {
		return nil
	}
	status := ""
	reason := event.Message
	switch strings.ToLower(event.Type) {
	case "desired_observed", "reconcile_success", "runtime_converged":
		status = "observed"
	case "desired_failed", "reconcile_failed", "runtime_failed":
		status = "failed"
	}
	if status == "" {
		return nil
	}
	if status == "observed" {
		_, err := tx.ExecContext(ctx, `
UPDATE control_intents
SET status = 'observed',
    updated_at = now(),
    observed_at = now(),
    failed_at = NULL,
    failure_reason = ''
WHERE runtime_id = $1
  AND desired_version = $2
  AND status IN ('accepted', 'projected', 'failed')`,
			event.RuntimeID, event.DesiredVersion)
		return err
	}
	_, err := tx.ExecContext(ctx, `
UPDATE control_intents
SET status = 'failed',
    updated_at = now(),
    failed_at = now(),
    failure_reason = $3
WHERE runtime_id = $1
  AND desired_version = $2
  AND status IN ('accepted', 'projected')`,
		event.RuntimeID, event.DesiredVersion, reason)
	return err
}

func ensureHostExistsTx(ctx context.Context, tx *sql.Tx, hostID string) error {
	var exists bool
	if err := tx.QueryRowContext(ctx, `SELECT EXISTS (SELECT 1 FROM hosts WHERE host_id = $1)`, hostID).Scan(&exists); err != nil {
		return err
	}
	if !exists {
		return errorsx.NotFound("host not found")
	}
	return nil
}

func ensureHostOnlineTx(ctx context.Context, tx *sql.Tx, hostID string, agentID string, at time.Time) error {
	if _, err := tx.ExecContext(ctx, `
INSERT INTO hosts (host_id, display_name, status, labels, last_heartbeat_at, created_at, updated_at)
VALUES ($1, $1, 'online', '{}'::jsonb, $2, now(), $2)
ON CONFLICT (host_id) DO UPDATE SET
  status = 'online',
  last_heartbeat_at = EXCLUDED.last_heartbeat_at,
  updated_at = EXCLUDED.updated_at`, hostID, at); err != nil {
		return err
	}
	if agentID != "" {
		if _, err := tx.ExecContext(ctx, `DELETE FROM agents WHERE host_id = $1 AND agent_id <> $2`, hostID, agentID); err != nil {
			return err
		}
		if _, err := tx.ExecContext(ctx, `
INSERT INTO agents (agent_id, host_id, agent_secret_hash, status, last_heartbeat_at, registered_at, updated_at)
VALUES ($1, $2, '', 'online', $3, now(), $3)
ON CONFLICT (agent_id) DO UPDATE SET
  host_id = EXCLUDED.host_id,
  status = 'online',
  last_heartbeat_at = EXCLUDED.last_heartbeat_at,
  updated_at = EXCLUDED.updated_at`, agentID, hostID, at); err != nil {
			return err
		}
	}
	return nil
}

func upsertObservedRuntimeTx(ctx context.Context, tx *sql.Tx, runtimeID string, hostID string) error {
	if hostID == "" {
		hostID = "host_unknown"
	}
	if _, err := tx.ExecContext(ctx, `
INSERT INTO runtimes (runtime_id, host_id, name, status, created_at, updated_at)
VALUES ($1, $2, $1, 'observed', now(), now())
ON CONFLICT (runtime_id) DO UPDATE SET
  host_id = EXCLUDED.host_id,
  updated_at = now()`, runtimeID, hostID); err != nil {
		return err
	}
	return nil
}

func nextDesiredVersion(ctx context.Context, tx *sql.Tx) (int64, error) {
	var version int64
	err := tx.QueryRowContext(ctx, `SELECT nextval('runtime_desired_version_seq')`).Scan(&version)
	return version, err
}

func nextIntentID(ctx context.Context, tx *sql.Tx) (string, error) {
	var seq int64
	if err := tx.QueryRowContext(ctx, `SELECT nextval('control_intent_seq')`).Scan(&seq); err != nil {
		return "", err
	}
	return fmt.Sprintf("intent_%s_%06d", time.Now().UTC().Format("20060102"), seq), nil
}

func updateDesiredTx(ctx context.Context, tx *sql.Tx, desired control.DesiredRuntime, version int64, actionIntent map[string]any) error {
	actionJSON, err := json.Marshal(actionIntent)
	if err != nil {
		return errorsx.BadRequest("action intent must be JSON serializable")
	}
	_, err = tx.ExecContext(ctx, `
UPDATE runtime_desired_states
SET desired_state = $2,
    config_version = $3,
    listen_port = $4,
    worker_count = $5,
    max_worker_count = $6,
    restart_policy = $7,
    draining = $8,
    action_intent = $9::jsonb,
    version = $10,
    generation = $10,
    updated_at = now()
WHERE runtime_id = $1`,
		desired.RuntimeID, string(desired.DesiredState), desired.ConfigVersion, desired.ListenPort,
		desired.WorkerCount, desired.MaxWorkers, string(desired.RestartPolicy), desired.Draining,
		string(actionJSON), version)
	if err != nil {
		return err
	}
	_, err = tx.ExecContext(ctx, `UPDATE runtimes SET updated_at = now() WHERE runtime_id = $1`, desired.RuntimeID)
	return err
}

func insertAuditTx(ctx context.Context, tx *sql.Tx, action string, targetType string, targetID string, payload map[string]any) error {
	if payload == nil {
		payload = map[string]any{}
	}
	payloadJSON, err := json.Marshal(payload)
	if err != nil {
		return errorsx.BadRequest("audit payload must be JSON serializable")
	}
	requestID := "audit_" + randomHex(8)
	_, err = tx.ExecContext(ctx, `
INSERT INTO operation_audit_logs
  (request_id, actor_type, actor_id, action, target_type, target_id, payload, created_at)
VALUES ($1, 'system', 'adminservice', $2, $3, $4, $5::jsonb, now())`,
		requestID, action, targetType, targetID, string(payloadJSON))
	return err
}

func (r *ControlRepository) withControl(runtime control.Runtime) control.Runtime {
	latestIntent, err := getLatestIntent(context.Background(), r.db, runtime.RuntimeID)
	if err != nil {
		return runtime
	}
	controlState := control.EvaluateRuntimeControl(runtime, latestIntent, time.Now().UTC(), control.DefaultRuntimeStaleAfter)
	runtime.Control = &controlState
	return runtime
}

func validateDesired(state control.DesiredState, listenPort int, workerCount int, maxWorkers int, policy control.RestartPolicy) error {
	switch state {
	case control.DesiredStateRunning, control.DesiredStateStopped, control.DesiredStateDraining, control.DesiredStateDeleted:
	default:
		return errorsx.BadRequest("desired_state must be running, stopped, draining, or deleted")
	}
	if listenPort <= 0 || listenPort > 65535 {
		return errorsx.BadRequest("listen_port must be within 1..65535")
	}
	if workerCount <= 0 {
		return errorsx.BadRequest("worker_count must be positive")
	}
	if maxWorkers <= 0 {
		return errorsx.BadRequest("max_worker_count must be positive")
	}
	if workerCount > maxWorkers {
		return errorsx.BadRequest("worker_count cannot exceed max_worker_count")
	}
	switch policy {
	case control.RestartPolicyNever, control.RestartPolicyOnFailure, control.RestartPolicyAlways:
		return nil
	case "":
		return errorsx.BadRequest("restart_policy is required")
	default:
		return errorsx.BadRequest("restart_policy must be never, on_failure, or always")
	}
}

func rollback(tx *sql.Tx) {
	_ = tx.Rollback()
}

func firstNonEmpty(values ...string) string {
	for _, value := range values {
		if strings.TrimSpace(value) != "" {
			return value
		}
	}
	return ""
}

func nullableString(value string) any {
	if value == "" {
		return nil
	}
	return value
}

func nullableInt(value int) any {
	if value == 0 {
		return nil
	}
	return value
}

func nullableIntPtr(value *int) any {
	if value == nil {
		return nil
	}
	return *value
}

func nullableInt64(value int64) any {
	if value == 0 {
		return nil
	}
	return value
}

func nullableTime(value time.Time) any {
	if value.IsZero() {
		return nil
	}
	return value
}

func randomHex(bytesLen int) string {
	buf := make([]byte, bytesLen)
	if _, err := rand.Read(buf); err != nil {
		return fmt.Sprintf("%d", time.Now().UTC().UnixNano())
	}
	return hex.EncodeToString(buf)
}
