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
	if client.sets[0].key != "config:runtime:rt_001" {
		t.Fatalf("projection key = %q", client.sets[0].key)
	}
	var desired RuntimeDesiredProjection
	if err := json.Unmarshal(client.sets[0].payload, &desired); err != nil {
		t.Fatalf("decode desired projection failed: %v", err)
	}
	if desired.Key != client.sets[0].key || desired.Version != 42 || desired.Runtime.RuntimeID != "rt_001" {
		t.Fatalf("unexpected desired projection: %#v", desired)
	}
	if len(client.publishes) != 1 {
		t.Fatalf("expected one config notify publish, got %d", len(client.publishes))
	}
	if client.publishes[0].channel != "control:config" {
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
	if client.sets[0].key != "control:desired-state:host_001" {
		t.Fatalf("host desired key = %q", client.sets[0].key)
	}
	if len(client.publishes) != 1 {
		t.Fatalf("expected one config notify publish, got %d", len(client.publishes))
	}
	var projection HostDesiredStateProjection
	if err := json.Unmarshal(client.sets[0].payload, &projection); err != nil {
		t.Fatalf("decode host desired projection failed: %v", err)
	}
	if projection.Key != client.sets[0].key || projection.Version != 43 || len(projection.Runtimes) != 2 {
		t.Fatalf("unexpected host desired projection: %#v", projection)
	}

	var notify ControlConfigNotify
	if err := json.Unmarshal(client.publishes[0].payload, &notify); err != nil {
		t.Fatalf("decode notify failed: %v", err)
	}
	if notify.Kind != "host_desired_state_updated" || notify.HostID != "host_001" || notify.Version != 43 {
		t.Fatalf("unexpected notify payload: %#v", notify)
	}
}

func TestRedisPublisherPublishesActionIntentProjection(t *testing.T) {
	client := &fakeRedisClient{configured: true}
	publisher := NewRedisPublisher(NewRegistry(redisStore.DefaultRegistry()), client)

	err := publisher.PublishActionIntent(control.ActionIntent{
		ID:             "intent_001",
		RequestID:      "req_001",
		RuntimeID:      "rt_001",
		HostID:         "host_001",
		Kind:           control.ActionKindStart,
		Status:         control.ActionIntentAccepted,
		DesiredVersion: 44,
		CreatedAt:      time.Now().UTC(),
		UpdatedAt:      time.Now().UTC(),
	}, control.DesiredRuntime{
		RuntimeID: "rt_001",
		HostID:    "host_001",
		Version:   44,
	})
	if err != nil {
		t.Fatalf("PublishActionIntent returned error: %v", err)
	}
	if len(client.sets) != 1 || client.sets[0].key != "control:intent:intent_001" {
		t.Fatalf("unexpected action intent SETs: %#v", client.sets)
	}
	var projection ActionIntentProjection
	if err := json.Unmarshal(client.sets[0].payload, &projection); err != nil {
		t.Fatalf("decode action intent projection failed: %v", err)
	}
	if projection.Key != client.sets[0].key || projection.Intent.ID != "intent_001" || projection.Runtime.Version != 44 {
		t.Fatalf("unexpected action intent projection: %#v", projection)
	}
	var notify ControlConfigNotify
	if err := json.Unmarshal(client.publishes[0].payload, &notify); err != nil {
		t.Fatalf("decode notify failed: %v", err)
	}
	if notify.Kind != "action_intent_projected" || notify.IntentID != "intent_001" || notify.DesiredVersion != 44 {
		t.Fatalf("unexpected action intent notify: %#v", notify)
	}
}

func TestRedisPublisherPublishesActualStatus(t *testing.T) {
	client := &fakeRedisClient{configured: true}
	publisher := NewRedisPublisher(NewRegistry(redisStore.DefaultRegistry()), client)

	err := publisher.PublishRuntimeActual(control.ActualSnapshot{
		RuntimeID:              "rt_001",
		HostID:                 "host_001",
		AgentID:                "ag_001",
		ActualState:            "stopped",
		ObservedDesiredVersion: 45,
		LastError:              "failed to stop",
		UpdatedAt:              time.Now().UTC(),
	})
	if err != nil {
		t.Fatalf("PublishRuntimeActual returned error: %v", err)
	}
	if len(client.sets) != 1 || client.sets[0].key != "runtime:actual:rt_001" || client.sets[0].ttl != 60*time.Second {
		t.Fatalf("unexpected actual SETs: %#v", client.sets)
	}
	var projection RuntimeActualProjection
	if err := json.Unmarshal(client.sets[0].payload, &projection); err != nil {
		t.Fatalf("decode actual projection failed: %v", err)
	}
	if projection.Status != "failed" || projection.LastError != "failed to stop" || projection.ObservedDesiredVersion != 45 {
		t.Fatalf("unexpected actual projection: %#v", projection)
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
