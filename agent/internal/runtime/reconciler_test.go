package runtime

import (
	"context"
	"testing"
	"time"
)

type fakeManager struct {
	actual   map[string]ActualState
	starts   int
	stops    int
	restarts int
}

func newFakeManager() *fakeManager {
	return &fakeManager{actual: make(map[string]ActualState)}
}

func (f *fakeManager) Actual(runtimeID string) (ActualState, bool) {
	actual, ok := f.actual[runtimeID]
	return actual, ok
}

func (f *fakeManager) Start(ctx context.Context, desired DesiredState) (ActualState, error) {
	f.starts++
	actual := ActualState{
		RuntimeID:              desired.RuntimeID,
		HostID:                 desired.HostID,
		ActualState:            ActualStateRunning,
		ConfigVersion:          desired.ConfigVersion,
		ConfigPath:             desired.ConfigPath,
		ObservedDesiredVersion: desired.Version,
		UpdatedAt:              time.Now().UTC(),
	}
	f.actual[desired.RuntimeID] = actual
	return actual, nil
}

func (f *fakeManager) Stop(ctx context.Context, runtimeID string, timeout time.Duration) (ActualState, error) {
	f.stops++
	actual := f.actual[runtimeID]
	actual.ActualState = ActualStateStopped
	actual.ProcessID = 0
	actual.UpdatedAt = time.Now().UTC()
	f.actual[runtimeID] = actual
	return actual, nil
}

func (f *fakeManager) Restart(ctx context.Context, desired DesiredState, timeout time.Duration) (ActualState, error) {
	f.restarts++
	actual := ActualState{
		RuntimeID:              desired.RuntimeID,
		HostID:                 desired.HostID,
		ActualState:            ActualStateRunning,
		ConfigVersion:          desired.ConfigVersion,
		ConfigPath:             desired.ConfigPath,
		ObservedDesiredVersion: desired.Version,
		UpdatedAt:              time.Now().UTC(),
	}
	f.actual[desired.RuntimeID] = actual
	return actual, nil
}

func TestDesiredReconcileStartRestartStopAndHostGuard(t *testing.T) {
	ctx := context.Background()
	manager := newFakeManager()
	renderCount := 0
	opts := ReconcileOptions{
		HostID:      "host-001",
		StopTimeout: time.Second,
		RenderConfig: func(ctx context.Context, desired DesiredState) (string, string, error) {
			renderCount++
			return "runtime/" + desired.RuntimeID + "/config.json", "checksum", nil
		},
	}

	desired := DesiredState{
		RuntimeID:     "rt-001",
		HostID:        "host-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		Version:       10,
		RestartPolicy: RestartPolicyOnFailure,
	}
	results := Reconcile(ctx, []DesiredState{desired}, manager, opts)
	if results[0].Action != ReconcileStart || manager.starts != 1 || renderCount != 1 {
		t.Fatalf("start reconcile mismatch: results=%#v starts=%d render=%d", results, manager.starts, renderCount)
	}
	if results[0].Actual.ConfigPath == "" {
		t.Fatalf("start did not receive rendered config path")
	}

	results = Reconcile(ctx, []DesiredState{desired}, manager, opts)
	if results[0].Action != ReconcileNoop || manager.starts != 1 {
		t.Fatalf("noop reconcile mismatch: results=%#v starts=%d", results, manager.starts)
	}

	desired.ConfigVersion = 2
	desired.Version = 11
	results = Reconcile(ctx, []DesiredState{desired}, manager, opts)
	if results[0].Action != ReconcileRestart || manager.restarts != 1 {
		t.Fatalf("restart reconcile mismatch: results=%#v restarts=%d", results, manager.restarts)
	}

	remote := desired
	remote.RuntimeID = "rt-remote"
	remote.HostID = "host-remote"
	results = Reconcile(ctx, []DesiredState{remote}, manager, opts)
	if results[0].Action != ReconcileSkip || manager.starts != 1 {
		t.Fatalf("remote host should be skipped: results=%#v starts=%d", results, manager.starts)
	}

	desired.DesiredState = DesiredStateStopped
	results = Reconcile(ctx, []DesiredState{desired}, manager, opts)
	if results[0].Action != ReconcileStop || manager.stops != 1 {
		t.Fatalf("stop reconcile mismatch: results=%#v stops=%d", results, manager.stops)
	}
}

func TestDesiredReconcileMultiRuntimeNoopAndGenerationRestart(t *testing.T) {
	ctx := context.Background()
	manager := newFakeManager()
	manager.actual["rt-001"] = ActualState{
		RuntimeID:              "rt-001",
		ActualState:            ActualStateRunning,
		ConfigVersion:          1,
		ObservedDesiredVersion: 10,
		UpdatedAt:              time.Now().UTC(),
	}

	results := Reconcile(ctx, []DesiredState{
		{
			RuntimeID:     "rt-001",
			DesiredState:  DesiredStateRunning,
			ConfigVersion: 1,
			Version:       10,
			Generation:    10,
		},
		{
			RuntimeID:     "rt-002",
			DesiredState:  DesiredStateRunning,
			ConfigVersion: 1,
			Version:       11,
			Generation:    11,
		},
	}, manager, ReconcileOptions{})

	if len(results) != 2 {
		t.Fatalf("result len = %d, want 2", len(results))
	}
	if results[0].Action != ReconcileNoop {
		t.Fatalf("rt-001 should be noop: %#v", results[0])
	}
	if results[1].Action != ReconcileStart || manager.starts != 1 {
		t.Fatalf("rt-002 should start: results=%#v starts=%d", results, manager.starts)
	}

	results = Reconcile(ctx, []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		Version:       12,
		Generation:    12,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileRestart || manager.restarts != 1 {
		t.Fatalf("generation bump should restart: results=%#v restarts=%d", results, manager.restarts)
	}
}

func TestDesiredReconcileStoppedObservedAndRestartPolicyAlways(t *testing.T) {
	manager := newFakeManager()
	manager.actual["rt-001"] = ActualState{
		RuntimeID:              "rt-001",
		ActualState:            ActualStateStopped,
		ConfigVersion:          1,
		ObservedDesiredVersion: 9,
		UpdatedAt:              time.Now().UTC(),
	}

	results := Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		RestartPolicy: RestartPolicyOnFailure,
		Version:       10,
		Generation:    10,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileStart || manager.starts != 1 {
		t.Fatalf("newer running desired should restart cleanly stopped runtime: results=%#v starts=%d", results, manager.starts)
	}

	manager.actual["rt-001"] = ActualState{
		RuntimeID:              "rt-001",
		ActualState:            ActualStateStopped,
		ConfigVersion:          1,
		ObservedDesiredVersion: 10,
		UpdatedAt:              time.Now().UTC(),
	}
	results = Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		RestartPolicy: RestartPolicyOnFailure,
		Version:       10,
		Generation:    10,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileNoop || manager.starts != 1 {
		t.Fatalf("same running desired should not restart cleanly stopped runtime: results=%#v starts=%d", results, manager.starts)
	}

	results = Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		RestartPolicy: RestartPolicyAlways,
		Version:       11,
		Generation:    11,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileStart || manager.starts != 2 {
		t.Fatalf("stopped runtime should restart with always: results=%#v starts=%d", results, manager.starts)
	}
}

func TestDesiredReconcileDeletedMissingReportsObservedStopped(t *testing.T) {
	manager := newFakeManager()
	results := Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		HostID:        "host-001",
		DesiredState:  DesiredStateDeleted,
		ConfigVersion: 1,
		Version:       14,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileNoop {
		t.Fatalf("missing deleted runtime should be noop: %#v", results[0])
	}
	if results[0].Actual.ActualState != ActualStateStopped || results[0].Actual.ObservedDesiredVersion != 14 {
		t.Fatalf("deleted missing runtime should report observed stopped: %#v", results[0].Actual)
	}
	if !results[0].Applied {
		t.Fatalf("deleted missing runtime should mark desired applied: %#v", results[0])
	}
}

func TestDesiredReconcileDoesNotRestartFailedRuntimeWhenPolicyNever(t *testing.T) {
	manager := newFakeManager()
	manager.actual["rt-001"] = ActualState{
		RuntimeID:              "rt-001",
		ActualState:            ActualStateFailed,
		ConfigVersion:          1,
		ObservedDesiredVersion: 5,
		UpdatedAt:              time.Now().UTC(),
	}
	results := Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		RestartPolicy: RestartPolicyNever,
		Version:       5,
		Generation:    5,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileNoop || manager.starts != 0 {
		t.Fatalf("failed runtime with restart_policy=never should not restart: results=%#v starts=%d", results, manager.starts)
	}

	results = Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateRunning,
		ConfigVersion: 1,
		RestartPolicy: RestartPolicyNever,
		Version:       6,
		Generation:    6,
	}}, manager, ReconcileOptions{})
	if results[0].Action != ReconcileStart || manager.starts != 1 {
		t.Fatalf("new desired should restart failed runtime even with restart_policy=never: results=%#v starts=%d", results, manager.starts)
	}
}

func TestDesiredReconcileStopsFailedRuntimeWithLiveProcess(t *testing.T) {
	manager := newFakeManager()
	manager.actual["rt-001"] = ActualState{
		RuntimeID:     "rt-001",
		ActualState:   ActualStateFailed,
		ProcessID:     1234,
		ConfigVersion: 1,
		UpdatedAt:     time.Now().UTC(),
	}
	results := Reconcile(context.Background(), []DesiredState{{
		RuntimeID:     "rt-001",
		DesiredState:  DesiredStateStopped,
		ConfigVersion: 1,
	}}, manager, ReconcileOptions{StopTimeout: time.Second})
	if results[0].Action != ReconcileStop || manager.stops != 1 {
		t.Fatalf("failed runtime with live process should be stopped: results=%#v stops=%d", results, manager.stops)
	}
}
