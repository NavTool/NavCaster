package projection

import (
	"context"
	"encoding/json"
	"strconv"
	"time"

	"navcaster-admin/internal/control"
	"navcaster-admin/internal/identity"
)

type RedisCommandClient interface {
	Configured() bool
	Set(ctx context.Context, key string, value []byte, ttl time.Duration) error
	Publish(ctx context.Context, channel string, value []byte) error
}

type RedisPublisher struct {
	registry Registry
	client   RedisCommandClient
}

func NewRedisPublisher(registry Registry, client RedisCommandClient) *RedisPublisher {
	return &RedisPublisher{registry: registry, client: client}
}

func (p *RedisPublisher) PublishRuntimeDesired(desired control.DesiredRuntime) error {
	if p == nil || p.client == nil || !p.client.Configured() {
		return nil
	}
	key, err := p.registry.RuntimeConfigKey(desired.RuntimeID)
	if err != nil {
		return err
	}
	payload := RuntimeDesiredProjection{
		Projection: "v2:config:runtime",
		Key:        key,
		Version:    desired.Version,
		Runtime:    desired,
		Published:  time.Now().UTC(),
	}
	if err := p.setJSON(key, payload, 0); err != nil {
		return err
	}
	return p.publishControlConfigNotify(ControlConfigNotify{
		Projection: "v2:control:config",
		Kind:       "runtime_desired_updated",
		Key:        key,
		RuntimeID:  desired.RuntimeID,
		HostID:     desired.HostID,
		Version:    desired.Version,
		Published:  time.Now().UTC(),
	})
}

func (p *RedisPublisher) PublishHostDesiredState(hostID string, states []control.DesiredRuntime) error {
	if p == nil || p.client == nil || !p.client.Configured() {
		return nil
	}
	key, err := p.registry.HostDesiredStateKey(hostID)
	if err != nil {
		return err
	}
	version := int64(0)
	for _, state := range states {
		if state.Version > version {
			version = state.Version
		}
	}
	payload := HostDesiredStateProjection{
		Projection:  "v2:control:desired-state",
		Key:         key,
		HostID:      hostID,
		Version:     version,
		Runtimes:    states,
		PublishedAt: time.Now().UTC(),
	}
	if err := p.setJSON(key, payload, 0); err != nil {
		return err
	}
	return p.publishControlConfigNotify(ControlConfigNotify{
		Projection: "v2:control:config",
		Kind:       "host_desired_state_updated",
		Key:        key,
		HostID:     hostID,
		Version:    version,
		Published:  time.Now().UTC(),
	})
}

func (p *RedisPublisher) PublishActionIntent(intent control.ActionIntent, desired control.DesiredRuntime) error {
	if p == nil || p.client == nil || !p.client.Configured() {
		return nil
	}
	key, err := p.registry.ActionIntentKey(intent.ID)
	if err != nil {
		return err
	}
	payload := ActionIntentProjection{
		Projection: "v2:control:intent",
		Key:        key,
		Intent:     intent,
		Runtime:    desired,
		Published:  time.Now().UTC(),
	}
	if err := p.setJSON(key, payload, 0); err != nil {
		return err
	}
	return p.publishControlConfigNotify(ControlConfigNotify{
		Projection:     "v2:control:config",
		Kind:           "action_intent_projected",
		Key:            key,
		RuntimeID:      intent.RuntimeID,
		HostID:         desired.HostID,
		IntentID:       intent.ID,
		IntentStatus:   string(intent.Status),
		Version:        intent.DesiredVersion,
		DesiredVersion: intent.DesiredVersion,
		Published:      time.Now().UTC(),
	})
}

func (p *RedisPublisher) PublishRuntimeActual(actual control.ActualSnapshot) error {
	if p == nil || p.client == nil || !p.client.Configured() {
		return nil
	}
	key, err := p.registry.RuntimeActualKey(actual.RuntimeID)
	if err != nil {
		return err
	}
	if actual.UpdatedAt.IsZero() {
		actual.UpdatedAt = time.Now().UTC()
	}
	payload := RuntimeActualProjection{
		Projection:             "v2:runtime:actual",
		Key:                    key,
		RuntimeID:              actual.RuntimeID,
		HostID:                 actual.HostID,
		AgentID:                actual.AgentID,
		ObservedDesiredVersion: actual.ObservedDesiredVersion,
		Status:                 runtimeActualProjectionStatus(actual),
		Stale:                  false,
		LastError:              actual.LastError,
		Runtime:                actual,
		Published:              time.Now().UTC(),
	}
	return p.setJSON(key, payload, 60*time.Second)
}

func (p *RedisPublisher) PublishAgentHeartbeat(heartbeat control.AgentHeartbeatProjection) error {
	if p == nil || p.client == nil || !p.client.Configured() {
		return nil
	}
	key, err := p.registry.AgentHeartbeatKey(heartbeat.AgentID)
	if err != nil {
		return err
	}
	if heartbeat.ExpiresAt.IsZero() && !heartbeat.ObservedAt.IsZero() {
		heartbeat.ExpiresAt = heartbeat.ObservedAt.Add(45 * time.Second)
	}
	return p.setJSON(key, heartbeat, 45*time.Second)
}

func (p *RedisPublisher) ProjectAccessAccount(ctx context.Context, projection identity.AccessAccountAuthProjection) error {
	if p == nil || p.client == nil || !p.client.Configured() {
		return nil
	}
	key, err := p.registry.AccessAccountAuthKey(projection.UsernameNorm)
	if err != nil {
		return err
	}
	if err := p.setJSONWithContext(ctx, key, projection, 0); err != nil {
		return err
	}
	versionKey, err := p.registry.AuthVersionKey()
	if err != nil {
		return err
	}
	if err := p.client.Set(ctx, versionKey, []byte(strconv.FormatInt(projection.ProjectionVersion, 10)), 0); err != nil {
		return err
	}
	channel, err := p.registry.ControlKickChannel()
	if err != nil {
		return err
	}
	payload, err := json.Marshal(AuthProjectionNotify{
		Projection:        "v2:auth:access-account",
		Kind:              "access_account_projection_updated",
		Key:               key,
		AccessAccountID:   projection.AccessAccountID,
		UsernameNorm:      projection.UsernameNorm,
		Status:            string(projection.Status),
		OwnerAccountID:    projection.OwnerAccountID,
		OwnerStatus:       string(projection.OwnerStatus),
		ProjectionVersion: projection.ProjectionVersion,
		Published:         time.Now().UTC(),
	})
	if err != nil {
		return err
	}
	return p.client.Publish(ctx, channel, payload)
}

func (p *RedisPublisher) setJSON(key string, value any, ttl time.Duration) error {
	payload, err := json.Marshal(value)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return p.client.Set(ctx, key, payload, ttl)
}

func (p *RedisPublisher) setJSONWithContext(ctx context.Context, key string, value any, ttl time.Duration) error {
	payload, err := json.Marshal(value)
	if err != nil {
		return err
	}
	if _, ok := ctx.Deadline(); ok {
		return p.client.Set(ctx, key, payload, ttl)
	}
	ctx, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()
	return p.client.Set(ctx, key, payload, ttl)
}

func (p *RedisPublisher) publishControlConfigNotify(value ControlConfigNotify) error {
	channel, err := p.registry.ConfigPubSubChannel()
	if err != nil {
		return err
	}
	payload, err := json.Marshal(value)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return p.client.Publish(ctx, channel, payload)
}

type RuntimeDesiredProjection struct {
	Projection string                 `json:"projection"`
	Key        string                 `json:"key"`
	Version    int64                  `json:"version"`
	Runtime    control.DesiredRuntime `json:"runtime"`
	Published  time.Time              `json:"published_at"`
}

type HostDesiredStateProjection struct {
	Projection  string                   `json:"projection"`
	Key         string                   `json:"key"`
	HostID      string                   `json:"host_id"`
	Version     int64                    `json:"version"`
	Runtimes    []control.DesiredRuntime `json:"runtimes"`
	PublishedAt time.Time                `json:"published_at"`
}

type ActionIntentProjection struct {
	Projection string                 `json:"projection"`
	Key        string                 `json:"key"`
	Intent     control.ActionIntent   `json:"intent"`
	Runtime    control.DesiredRuntime `json:"runtime"`
	Published  time.Time              `json:"published_at"`
}

type RuntimeActualProjection struct {
	Projection             string                 `json:"projection"`
	Key                    string                 `json:"key"`
	RuntimeID              string                 `json:"runtime_id"`
	HostID                 string                 `json:"host_id"`
	AgentID                string                 `json:"agent_id,omitempty"`
	ObservedDesiredVersion int64                  `json:"observed_desired_version"`
	Status                 string                 `json:"status"`
	Stale                  bool                   `json:"stale"`
	LastError              string                 `json:"last_error,omitempty"`
	Runtime                control.ActualSnapshot `json:"runtime"`
	Published              time.Time              `json:"published_at"`
}

type ControlConfigNotify struct {
	Projection     string    `json:"projection"`
	Kind           string    `json:"kind"`
	Key            string    `json:"key"`
	RuntimeID      string    `json:"runtime_id,omitempty"`
	HostID         string    `json:"host_id,omitempty"`
	IntentID       string    `json:"intent_id,omitempty"`
	IntentStatus   string    `json:"intent_status,omitempty"`
	Version        int64     `json:"version"`
	DesiredVersion int64     `json:"desired_version,omitempty"`
	Published      time.Time `json:"published_at"`
}

type AuthProjectionNotify struct {
	Projection        string    `json:"projection"`
	Kind              string    `json:"kind"`
	Key               string    `json:"key"`
	AccessAccountID   string    `json:"access_account_id"`
	UsernameNorm      string    `json:"username_norm"`
	Status            string    `json:"status"`
	OwnerAccountID    string    `json:"owner_account_id"`
	OwnerStatus       string    `json:"owner_status"`
	ProjectionVersion int64     `json:"projection_version"`
	Published         time.Time `json:"published_at"`
}

func runtimeActualProjectionStatus(actual control.ActualSnapshot) string {
	if actual.LastError != "" {
		return "failed"
	}
	return "observed"
}
