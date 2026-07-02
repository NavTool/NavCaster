package control

import (
	"testing"
	"time"
)

func TestRuntimeDesiredStateAndActionIntent(t *testing.T) {
	repo := NewMemoryRepository()
	service := NewService(repo)

	runtime, err := service.CreateRuntime(RuntimeCreateRequest{
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
	if runtime.Desired == nil || runtime.Desired.DesiredState != DesiredStateRunning {
		t.Fatalf("expected running desired state, got %#v", runtime.Desired)
	}
	firstVersion := runtime.Desired.Version

	intent, runtime, err := service.Drain(runtime.RuntimeID, ActionRequest{RequestID: "req-drain"})
	if err != nil {
		t.Fatalf("Drain returned error: %v", err)
	}
	if intent.Status != ActionIntentAccepted {
		t.Fatalf("intent status = %q", intent.Status)
	}
	if runtime.Desired.DesiredState != DesiredStateDraining || !runtime.Desired.Draining {
		t.Fatalf("expected draining desired state, got %#v", runtime.Desired)
	}
	if runtime.Desired.Version <= firstVersion {
		t.Fatalf("desired version did not increase")
	}

	intent, runtime, err = service.Restart(runtime.RuntimeID, ActionRequest{RequestID: "req-restart"})
	if err != nil {
		t.Fatalf("Restart returned error: %v", err)
	}
	if intent.ID == "" || intent.DesiredVersion != runtime.Desired.Version {
		t.Fatalf("unexpected intent: %#v runtime desired: %#v", intent, runtime.Desired)
	}
	if runtime.Desired.DesiredState != DesiredStateRunning {
		t.Fatalf("restart intent must keep steady desired running, got %s", runtime.Desired.DesiredState)
	}
	intents, err := service.ListActionIntents(runtime.RuntimeID, 10)
	if err != nil {
		t.Fatalf("ListActionIntents returned error: %v", err)
	}
	if len(intents) != 2 {
		t.Fatalf("expected two intents, got %#v", intents)
	}
	if intents[0].Status != ActionIntentAccepted || intents[1].Status != ActionIntentSuperseded {
		t.Fatalf("unexpected intent lifecycle: %#v", intents)
	}
}

func TestCreateRuntimeValidatesWorkerBounds(t *testing.T) {
	repo := NewMemoryRepository()
	service := NewService(repo)
	_, err := service.CreateRuntime(RuntimeCreateRequest{
		HostID:        "host_local",
		ConfigVersion: 1,
		ListenPort:    4202,
		WorkerCount:   8,
		MaxWorkers:    4,
		RestartPolicy: RestartPolicyOnFailure,
	})
	if err == nil {
		t.Fatal("expected validation error")
	}
}

func TestRuntimeControlStatusFromActual(t *testing.T) {
	repo := NewMemoryRepository()
	service := NewService(repo)

	runtime, err := service.CreateRuntime(RuntimeCreateRequest{
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
	intent, runtime, err := service.Stop(runtime.RuntimeID, ActionRequest{RequestID: "req-stop"})
	if err != nil {
		t.Fatalf("Stop returned error: %v", err)
	}
	if err := repo.ApplyActualSnapshots("ag_001", "host_local", []ActualSnapshot{{
		RuntimeID:              runtime.RuntimeID,
		ActualState:            "stopped",
		ObservedDesiredVersion: intent.DesiredVersion,
		UpdatedAt:              time.Now().UTC(),
	}}); err != nil {
		t.Fatalf("ApplyActualSnapshots returned error: %v", err)
	}
	updated, err := service.GetRuntime(runtime.RuntimeID)
	if err != nil {
		t.Fatalf("GetRuntime returned error: %v", err)
	}
	if updated.Control == nil || updated.Control.Status != RuntimeControlConverged {
		t.Fatalf("expected converged control state, got %#v", updated.Control)
	}
	intents, err := service.ListActionIntents(runtime.RuntimeID, 1)
	if err != nil {
		t.Fatalf("ListActionIntents returned error: %v", err)
	}
	if len(intents) != 1 || intents[0].Status != ActionIntentObserved {
		t.Fatalf("expected observed intent, got %#v", intents)
	}

	intent, runtime, err = service.Start(runtime.RuntimeID, ActionRequest{RequestID: "req-start"})
	if err != nil {
		t.Fatalf("Start returned error: %v", err)
	}
	if err := repo.ApplyActualSnapshots("ag_001", "host_local", []ActualSnapshot{{
		RuntimeID:              runtime.RuntimeID,
		ActualState:            "stopped",
		ObservedDesiredVersion: intent.DesiredVersion,
		LastError:              "start failed",
		UpdatedAt:              time.Now().UTC(),
	}}); err != nil {
		t.Fatalf("ApplyActualSnapshots returned error: %v", err)
	}
	updated, err = service.GetRuntime(runtime.RuntimeID)
	if err != nil {
		t.Fatalf("GetRuntime returned error: %v", err)
	}
	if updated.Control == nil || updated.Control.Status != RuntimeControlFailed || updated.Control.Reason != "actual_error" {
		t.Fatalf("expected failed control state, got %#v", updated.Control)
	}
}
