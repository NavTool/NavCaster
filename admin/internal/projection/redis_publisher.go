package projection

import (
	"context"
	"encoding/json"
	"time"

	"navcaster-admin/internal/control"
	redisStore "navcaster-admin/internal/storage/redis"
)

type RedisPublisher struct {
	registry Registry
	client   *redisStore.Client
}

func NewRedisPublisher(registry Registry, client *redisStore.Client) *RedisPublisher {
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
		Version:    desired.Version,
		Runtime:    desired,
		Published:  time.Now().UTC(),
	}
	return p.setJSON(key, payload, 0)
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
		HostID:      hostID,
		Version:     version,
		Runtimes:    states,
		PublishedAt: time.Now().UTC(),
	}
	return p.setJSON(key, payload, 0)
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
		Projection: "v2:runtime:actual",
		Runtime:    actual,
		Published:  time.Now().UTC(),
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

func (p *RedisPublisher) setJSON(key string, value any, ttl time.Duration) error {
	payload, err := json.Marshal(value)
	if err != nil {
		return err
	}
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return p.client.Set(ctx, key, payload, ttl)
}

type RuntimeDesiredProjection struct {
	Projection string                 `json:"projection"`
	Version    int64                  `json:"version"`
	Runtime    control.DesiredRuntime `json:"runtime"`
	Published  time.Time              `json:"published_at"`
}

type HostDesiredStateProjection struct {
	Projection  string                   `json:"projection"`
	HostID      string                   `json:"host_id"`
	Version     int64                    `json:"version"`
	Runtimes    []control.DesiredRuntime `json:"runtimes"`
	PublishedAt time.Time                `json:"published_at"`
}

type RuntimeActualProjection struct {
	Projection string                 `json:"projection"`
	Runtime    control.ActualSnapshot `json:"runtime"`
	Published  time.Time              `json:"published_at"`
}
