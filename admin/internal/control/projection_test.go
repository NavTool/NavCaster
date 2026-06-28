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

	intent, _, err := repo.RecordActionIntent(runtime.RuntimeID, ActionKindDrain, ActionRequest{RequestID: "req-drain", Payload: map[string]any{"draining": true}})
	if err != nil {
		t.Fatalf("RecordActionIntent returned error: %v", err)
	}
	if intent.Status != ActionIntentProjected {
		t.Fatalf("expected projected intent, got %#v", intent)
	}
	if publisher.desiredCount != 2 || publisher.hostDesiredCount != 2 || publisher.intentCount != 1 {
		t.Fatalf("expected second desired projection, got desired=%d host=%d intent=%d", publisher.desiredCount, publisher.hostDesiredCount, publisher.intentCount)
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
		ObservedDesiredVersion: intent.DesiredVersion,
		UpdatedAt:              now,
	}}); err != nil {
		t.Fatalf("ApplyActualSnapshots returned error: %v", err)
	}
	if publisher.actualCount != 1 {
		t.Fatalf("expected actual projection, got %d", publisher.actualCount)
	}
	if publisher.intentCount != 2 || publisher.lastIntentStatus != ActionIntentObserved {
		t.Fatalf("expected observed intent reprojection, got count=%d status=%s", publisher.intentCount, publisher.lastIntentStatus)
	}
}

type fakePublisher struct {
	desiredCount     int
	hostDesiredCount int
	intentCount      int
	actualCount      int
	heartbeatCount   int
	lastIntentStatus ActionIntentStatus
}

func (p *fakePublisher) PublishRuntimeDesired(DesiredRuntime) error {
	p.desiredCount++
	return nil
}

func (p *fakePublisher) PublishHostDesiredState(string, []DesiredRuntime) error {
	p.hostDesiredCount++
	return nil
}

func (p *fakePublisher) PublishActionIntent(intent ActionIntent, _ DesiredRuntime) error {
	p.intentCount++
	p.lastIntentStatus = intent.Status
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
