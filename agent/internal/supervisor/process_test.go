package supervisor

import (
	"context"
	"os"
	"testing"
	"time"

	agentruntime "navcaster/agent/internal/runtime"
)

func TestSupervisorDummyCommandStartStopRestart(t *testing.T) {
	if os.Getenv("NAVCASTER_AGENT_TEST_HELPER") == "1" {
		select {}
	}

	ctx := context.Background()
	sup := NewProcessSupervisor()
	desired := agentruntime.DesiredState{
		RuntimeID:     "rt-dummy",
		HostID:        "host-001",
		DesiredState:  agentruntime.DesiredStateRunning,
		RuntimeKind:   agentruntime.RuntimeKindCommand,
		ConfigVersion: 1,
		Command:       os.Args[0],
		Args:          []string{"-test.run=TestSupervisorDummyCommandStartStopRestart"},
		Env: map[string]string{
			"NAVCASTER_AGENT_TEST_HELPER": "1",
		},
	}

	started, err := sup.Start(ctx, desired)
	if err != nil {
		t.Fatalf("Start() error = %v", err)
	}
	if started.ProcessID == 0 || started.ActualState != agentruntime.ActualStateRunning {
		t.Fatalf("unexpected started actual state: %#v", started)
	}
	if actual, ok := sup.Actual("rt-dummy"); !ok || actual.StartToken == "" {
		t.Fatalf("Actual() missing managed runtime: actual=%#v ok=%v", actual, ok)
	}

	restarted, err := sup.Restart(ctx, desired, 2*time.Second)
	if err != nil {
		t.Fatalf("Restart() error = %v", err)
	}
	if restarted.ProcessID == 0 || restarted.ProcessID == started.ProcessID {
		t.Fatalf("restart did not create a new process: before=%d after=%d", started.ProcessID, restarted.ProcessID)
	}

	stopped, err := sup.Stop(ctx, "rt-dummy", 2*time.Second)
	if err != nil {
		t.Fatalf("Stop() error = %v", err)
	}
	if stopped.ActualState != agentruntime.ActualStateStopped {
		t.Fatalf("stopped actual state = %s, want stopped", stopped.ActualState)
	}
	if _, ok := sup.Actual("rt-dummy"); ok {
		t.Fatalf("runtime should not remain managed after Stop()")
	}
}

func TestSupervisorDoesNotStopUnmanagedProcess(t *testing.T) {
	sup := NewProcessSupervisor()
	if _, err := sup.Stop(context.Background(), "not-owned", time.Millisecond); err != ErrNotManaged {
		t.Fatalf("Stop unmanaged error = %v, want ErrNotManaged", err)
	}
}

func TestSupervisorDoesNotStopCachedUnownedRuntime(t *testing.T) {
	sup := NewProcessSupervisor()
	sup.ObserveCachedActual(agentruntime.ActualState{
		RuntimeID:   "rt-cached",
		ActualState: agentruntime.ActualStateRunning,
		ProcessID:   12345,
		StartToken:  "cached-token",
		UpdatedAt:   time.Now().UTC(),
	})

	actual, ok := sup.Actual("rt-cached")
	if !ok || actual.ProcessID != 12345 {
		t.Fatalf("cached actual missing: actual=%#v ok=%v", actual, ok)
	}
	stopped, err := sup.Stop(context.Background(), "rt-cached", time.Millisecond)
	if err != ErrNotManaged {
		t.Fatalf("Stop cached unowned error = %v, want ErrNotManaged", err)
	}
	if stopped.ActualState != agentruntime.ActualStateRunning || stopped.ProcessID != 12345 {
		t.Fatalf("cached unowned actual should remain observed only: %#v", stopped)
	}
}
