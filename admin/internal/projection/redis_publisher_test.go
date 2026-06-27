package projection

import (
	"context"
	"encoding/json"
	"testing"
	"time"

	"navcaster-admin/internal/control"
	redisStore "navcaster-admin/internal/storage/redis"
)

func TestRedisPublisherPublishesControlConfigNotify(t *testing.T) {
	client := &fakeRedisClient{configured: true}
	publisher := NewRedisPublisher(NewRegistry(redisStore.DefaultRegistry()), client)

	err := publisher.PublishRuntimeDesired(control.DesiredRuntime{
		RuntimeID:     "rt_001",
		HostID:        "host_001",
		DesiredState:  control.DesiredStateRunning,
		ConfigVersion: 17,
		ListenPort:    4202,
		WorkerCount:   2,
		MaxWorkers:    8,
		RestartPolicy: control.RestartPolicyOnFailure,
		Version:       42,
		UpdatedAt:     time.Date(2026, 6, 27, 0, 0, 0, 0, time.UTC),
	})
	if err != nil {
		t.Fatalf("PublishRuntimeDesired returned error: %v", err)
	}
	if len(client.sets) != 1 {
		t.Fatalf("expected one projection SET, got %d", len(client.sets))
	}
	if client.sets[0].key != "v2:config:runtime:rt_001" {
		t.Fatalf("projection key = %q", client.sets[0].key)
	}
	if len(client.publishes) != 1 {
		t.Fatalf("expected one config notify publish, got %d", len(client.publishes))
	}
	if client.publishes[0].channel != "v2:control:config" {
		t.Fatalf("notify channel = %q", client.publishes[0].channel)
	}

	var notify ControlConfigNotify
	if err := json.Unmarshal(client.publishes[0].payload, &notify); err != nil {
		t.Fatalf("decode notify failed: %v", err)
	}
	if notify.Kind != "runtime_desired_updated" || notify.RuntimeID != "rt_001" || notify.HostID != "host_001" || notify.Version != 42 {
		t.Fatalf("unexpected notify payload: %#v", notify)
	}
}

func TestRedisPublisherPublishesHostDesiredStateNotify(t *testing.T) {
	client := &fakeRedisClient{configured: true}
	publisher := NewRedisPublisher(NewRegistry(redisStore.DefaultRegistry()), client)

	err := publisher.PublishHostDesiredState("host_001", []control.DesiredRuntime{
		{RuntimeID: "rt_001", HostID: "host_001", Version: 41},
		{RuntimeID: "rt_002", HostID: "host_001", Version: 43},
	})
	if err != nil {
		t.Fatalf("PublishHostDesiredState returned error: %v", err)
	}
	if len(client.sets) != 1 {
		t.Fatalf("expected one host desired SET, got %d", len(client.sets))
	}
	if client.sets[0].key != "v2:control:desired-state:host_001" {
		t.Fatalf("host desired key = %q", client.sets[0].key)
	}
	if len(client.publishes) != 1 {
		t.Fatalf("expected one config notify publish, got %d", len(client.publishes))
	}

	var notify ControlConfigNotify
	if err := json.Unmarshal(client.publishes[0].payload, &notify); err != nil {
		t.Fatalf("decode notify failed: %v", err)
	}
	if notify.Kind != "host_desired_state_updated" || notify.HostID != "host_001" || notify.Version != 43 {
		t.Fatalf("unexpected notify payload: %#v", notify)
	}
}

type fakeRedisClient struct {
	configured bool
	sets       []fakeRedisSet
	publishes  []fakeRedisPublish
}

type fakeRedisSet struct {
	key     string
	payload []byte
	ttl     time.Duration
}

type fakeRedisPublish struct {
	channel string
	payload []byte
}

func (c *fakeRedisClient) Configured() bool {
	return c.configured
}

func (c *fakeRedisClient) Set(_ context.Context, key string, value []byte, ttl time.Duration) error {
	c.sets = append(c.sets, fakeRedisSet{key: key, payload: append([]byte(nil), value...), ttl: ttl})
	return nil
}

func (c *fakeRedisClient) Publish(_ context.Context, channel string, value []byte) error {
	c.publishes = append(c.publishes, fakeRedisPublish{channel: channel, payload: append([]byte(nil), value...)})
	return nil
}
