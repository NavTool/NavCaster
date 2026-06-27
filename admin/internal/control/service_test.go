package control

import "testing"

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

	intent, runtime, err := service.Drain(runtime.RuntimeID)
	if err != nil {
		t.Fatalf("Drain returned error: %v", err)
	}
	if intent.Status != "accepted" {
		t.Fatalf("intent status = %q", intent.Status)
	}
	if runtime.Desired.DesiredState != DesiredStateDraining || !runtime.Desired.Draining {
		t.Fatalf("expected draining desired state, got %#v", runtime.Desired)
	}
	if runtime.Desired.Version <= firstVersion {
		t.Fatalf("desired version did not increase")
	}

	intent, runtime, err = service.Restart(runtime.RuntimeID)
	if err != nil {
		t.Fatalf("Restart returned error: %v", err)
	}
	if intent.ID == "" || intent.DesiredVersion != runtime.Desired.Version {
		t.Fatalf("unexpected intent: %#v runtime desired: %#v", intent, runtime.Desired)
	}
	if runtime.Desired.DesiredState != DesiredStateRunning {
		t.Fatalf("restart intent must keep steady desired running, got %s", runtime.Desired.DesiredState)
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
