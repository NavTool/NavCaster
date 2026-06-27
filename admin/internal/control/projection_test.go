package control

import (
	"testing"
	"time"
)

func TestProjectingRepositoryPublishesDesiredAndActual(t *testing.T) {
	publisher := &fakePublisher{}
	repo := NewProjectingRepository(NewMemoryRepository(), publisher)

	runtime, err := repo.CreateRuntime(RuntimeCreateRequest{
		HostID:           "host_local",
		Name:             "caster-a",
		ConfigVersion:    17,
		ListenPort:       4202,
		WorkerCount:      4,
		MaxWorkers:       16,
		RestartPolicy:    RestartPolicyOnFailure,
		StartImmediately: true,
	})
	if err != nil {
		t.Fatalf("CreateRuntime returned error: %v", err)
	}
	if publisher.desiredCount != 1 || publisher.hostDesiredCount != 1 {
		t.Fatalf("expected desired projection, got desired=%d host=%d", publisher.desiredCount, publisher.hostDesiredCount)
	}

	if _, _, err := repo.RecordActionIntent(runtime.RuntimeID, ActionKindDrain, map[string]any{"draining": true}); err != nil {
		t.Fatalf("RecordActionIntent returned error: %v", err)
	}
	if publisher.desiredCount != 2 || publisher.hostDesiredCount != 2 {
		t.Fatalf("expected second desired projection, got desired=%d host=%d", publisher.desiredCount, publisher.hostDesiredCount)
	}

	now := time.Now().UTC()
	if err := repo.ApplyHeartbeat("host_local", "ag_001", now); err != nil {
		t.Fatalf("ApplyHeartbeat returned error: %v", err)
	}
	if publisher.heartbeatCount != 1 {
		t.Fatalf("expected heartbeat projection, got %d", publisher.heartbeatCount)
	}

	if err := repo.ApplyActualSnapshots("ag_001", "host_local", []ActualSnapshot{{
		RuntimeID:              runtime.RuntimeID,
		ActualState:            "running",
		ProcessID:              1234,
		ObservedDesiredVersion: runtime.Desired.Version,
		UpdatedAt:              now,
	}}); err != nil {
		t.Fatalf("ApplyActualSnapshots returned error: %v", err)
	}
	if publisher.actualCount != 1 {
		t.Fatalf("expected actual projection, got %d", publisher.actualCount)
	}
}

type fakePublisher struct {
	desiredCount     int
	hostDesiredCount int
	actualCount      int
	heartbeatCount   int
}

func (p *fakePublisher) PublishRuntimeDesired(DesiredRuntime) error {
	p.desiredCount++
	return nil
}

func (p *fakePublisher) PublishHostDesiredState(string, []DesiredRuntime) error {
	p.hostDesiredCount++
	return nil
}

func (p *fakePublisher) PublishRuntimeActual(ActualSnapshot) error {
	p.actualCount++
	return nil
}

func (p *fakePublisher) PublishAgentHeartbeat(AgentHeartbeatProjection) error {
	p.heartbeatCount++
	return nil
}
