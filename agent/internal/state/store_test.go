package state

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	agentruntime "navcaster/agent/internal/runtime"
)

func TestLocalStateCacheRoundTrip(t *testing.T) {
	root := t.TempDir()
	store := NewStoreWithRuntimeRoot(filepath.Join(root, "agent_state.json"), filepath.Join(root, "runtime"))
	now := time.Now().UTC()
	original := NewAgentState()
	original.AgentID = "ag-001"
	original.AgentSecret = "secret"
	original.HostID = "host-001"
	original.ApplyDesired(agentruntime.DesiredDocument{
		Version: 7,
		Runtimes: []agentruntime.DesiredState{{
			RuntimeID:     "rt-001",
			HostID:        "host-001",
			DesiredState:  agentruntime.DesiredStateRunning,
			ConfigVersion: 3,
			ListenPort:    4202,
			WorkerCount:   4,
			RestartPolicy: agentruntime.RestartPolicyOnFailure,
			UpdatedAt:     now,
		}},
		UpdatedAt: now,
	})
	original.UpdateActual(agentruntime.ActualState{
		RuntimeID:     "rt-001",
		HostID:        "host-001",
		AgentID:       "ag-001",
		ActualState:   agentruntime.ActualStateRunning,
		ProcessID:     1234,
		ConfigVersion: 3,
		UpdatedAt:     now,
	})
	original.AppendEvent(agentruntime.Event{RuntimeID: "rt-001", Type: "process_started", OccurredAt: now}, 10)

	if err := store.Save(original); err != nil {
		t.Fatalf("Save() error = %v", err)
	}

	loaded, err := store.Load()
	if err != nil {
		t.Fatalf("Load() error = %v", err)
	}
	if loaded.AgentID != original.AgentID || loaded.AgentSecret != original.AgentSecret || loaded.HostID != original.HostID {
		t.Fatalf("loaded identity mismatch: %#v", loaded)
	}
	if loaded.LastDesiredVersion != 7 {
		t.Fatalf("LastDesiredVersion = %d, want 7", loaded.LastDesiredVersion)
	}
	cache := loaded.Runtimes["rt-001"]
	if cache.Desired.ConfigVersion != 3 || cache.Actual.ProcessID != 1234 {
		t.Fatalf("runtime cache mismatch: %#v", cache)
	}
	if len(cache.Events) != 1 || cache.Events[0].Type != "process_started" {
		t.Fatalf("events mismatch: %#v", cache.Events)
	}
	if err := storeFileExists(filepath.Join(root, "runtime", "runtimes", "rt-001", "desired.json")); err != nil {
		t.Fatalf("desired runtime cache missing: %v", err)
	}
	if err := storeFileExists(filepath.Join(root, "runtime", "runtimes", "rt-001", "actual.json")); err != nil {
		t.Fatalf("actual runtime cache missing: %v", err)
	}
	if err := storeFileExists(filepath.Join(root, "runtime", "runtimes", "rt-001", "events.log")); err != nil {
		t.Fatalf("event runtime cache missing: %v", err)
	}
}

func TestBufferedEventsAndClearEvents(t *testing.T) {
	state := NewAgentState()
	now := time.Now().UTC()
	state.AppendEvent(agentruntime.Event{RuntimeID: "rt-001", Type: "process_started", OccurredAt: now}, 10)
	state.AppendEvent(agentruntime.Event{RuntimeID: "rt-002", Type: "process_exited", OccurredAt: now}, 10)

	events := state.BufferedEvents()
	if len(events) != 2 {
		t.Fatalf("BufferedEvents() len = %d, want 2", len(events))
	}
	state.ClearEvents()
	if events := state.BufferedEvents(); len(events) != 0 {
		t.Fatalf("BufferedEvents() after ClearEvents len = %d, want 0", len(events))
	}
}

func TestApplyDesiredKeepsCachedRuntimeOnEmptyIncrementalPoll(t *testing.T) {
	state := NewAgentState()
	state.ApplyDesired(agentruntime.DesiredDocument{
		Version: 7,
		Runtimes: []agentruntime.DesiredState{{
			RuntimeID:     "rt-001",
			HostID:        "host-001",
			DesiredState:  agentruntime.DesiredStateRunning,
			ConfigVersion: 3,
			ListenPort:    4202,
			WorkerCount:   4,
			RestartPolicy: agentruntime.RestartPolicyOnFailure,
		}},
	})

	state.ApplyDesired(agentruntime.DesiredDocument{Version: 7, Runtimes: nil})

	if state.LastDesiredVersion != 7 {
		t.Fatalf("LastDesiredVersion = %d, want 7", state.LastDesiredVersion)
	}
	if len(state.Desired.Runtimes) != 1 || state.Desired.Runtimes[0].RuntimeID != "rt-001" {
		t.Fatalf("desired runtime cache was not preserved: %#v", state.Desired.Runtimes)
	}
	if state.Runtimes["rt-001"].Desired.ConfigVersion != 3 {
		t.Fatalf("runtime cache mismatch: %#v", state.Runtimes["rt-001"])
	}
}

func TestApplyDesiredIgnoresStaleDocument(t *testing.T) {
	state := NewAgentState()
	state.ApplyDesired(agentruntime.DesiredDocument{
		Version: 7,
		Runtimes: []agentruntime.DesiredState{{
			RuntimeID:     "rt-001",
			DesiredState:  agentruntime.DesiredStateRunning,
			ConfigVersion: 3,
		}},
	})

	state.ApplyDesired(agentruntime.DesiredDocument{
		Version: 6,
		Runtimes: []agentruntime.DesiredState{{
			RuntimeID:     "rt-001",
			DesiredState:  agentruntime.DesiredStateStopped,
			ConfigVersion: 2,
		}},
	})

	if state.LastDesiredVersion != 7 || state.Runtimes["rt-001"].Desired.DesiredState != agentruntime.DesiredStateRunning {
		t.Fatalf("stale document changed state: version=%d cache=%#v", state.LastDesiredVersion, state.Runtimes["rt-001"])
	}
}

func TestMarkDesiredAppliedClearsDeletedDesiredButKeepsActual(t *testing.T) {
	state := NewAgentState()
	state.ApplyDesired(agentruntime.DesiredDocument{
		Version: 8,
		Runtimes: []agentruntime.DesiredState{{
			RuntimeID:     "rt-001",
			DesiredState:  agentruntime.DesiredStateDeleted,
			ConfigVersion: 3,
			Version:       8,
		}},
	})
	state.UpdateActual(agentruntime.ActualState{
		RuntimeID:     "rt-001",
		ActualState:   agentruntime.ActualStateStopped,
		ConfigVersion: 3,
		UpdatedAt:     time.Now().UTC(),
	})

	state.MarkDesiredApplied("rt-001", 8)

	cache := state.Runtimes["rt-001"]
	if cache.Desired.RuntimeID != "" {
		t.Fatalf("deleted desired should be cleared after apply: %#v", cache.Desired)
	}
	if cache.Actual.RuntimeID != "rt-001" || cache.Actual.ObservedDesiredVersion != 8 {
		t.Fatalf("actual should remain with observed version: %#v", cache.Actual)
	}
	if len(state.DesiredStates()) != 0 {
		t.Fatalf("deleted runtime should not remain in desired states: %#v", state.DesiredStates())
	}
}

func storeFileExists(path string) error {
	_, err := os.Stat(path)
	return err
}
