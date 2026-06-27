package supervisor

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
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

func TestSupervisorRecordsStartFailureEvent(t *testing.T) {
	sup := NewProcessSupervisor()
	actual, err := sup.Start(context.Background(), agentruntime.DesiredState{
		RuntimeID:     "rt-fail",
		DesiredState:  agentruntime.DesiredStateRunning,
		RuntimeKind:   agentruntime.RuntimeKindCommand,
		ConfigVersion: 1,
		Version:       5,
	})
	if err == nil {
		t.Fatalf("Start() should fail without command")
	}
	if actual.ActualState != agentruntime.ActualStateFailed || actual.ObservedDesiredVersion != 5 {
		t.Fatalf("unexpected failure actual: %#v", actual)
	}
	events := sup.DrainEvents()
	if len(events) != 1 || events[0].Type != "process_start_failed" || events[0].Severity != "error" {
		t.Fatalf("expected start failure event: %#v", events)
	}
}

func TestSupervisorProcessExitCapturesFailureAndRestartEvent(t *testing.T) {
	if os.Getenv("NAVCASTER_AGENT_TEST_EXIT") == "1" {
		fmt.Fprint(os.Stderr, "intentional stderr tail")
		os.Exit(7)
	}

	sup := NewProcessSupervisor()
	started, err := sup.Start(context.Background(), agentruntime.DesiredState{
		RuntimeID:     "rt-exit",
		DesiredState:  agentruntime.DesiredStateRunning,
		RuntimeKind:   agentruntime.RuntimeKindCommand,
		ConfigVersion: 1,
		Command:       os.Args[0],
		Args:          []string{"-test.run=TestSupervisorProcessExitCapturesFailureAndRestartEvent"},
		Env: map[string]string{
			"NAVCASTER_AGENT_TEST_EXIT": "1",
		},
		RestartPolicy: agentruntime.RestartPolicyOnFailure,
		Version:       6,
	})
	if err != nil {
		t.Fatalf("Start() error = %v", err)
	}
	if started.ProcessID == 0 {
		t.Fatalf("expected process id: %#v", started)
	}

	deadline := time.Now().Add(3 * time.Second)
	var actual agentruntime.ActualState
	for time.Now().Before(deadline) {
		var ok bool
		actual, ok = sup.Actual("rt-exit")
		if ok && actual.ActualState == agentruntime.ActualStateFailed {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if actual.ActualState != agentruntime.ActualStateFailed || actual.LastExitCode == nil || *actual.LastExitCode != 7 {
		t.Fatalf("unexpected exit actual: %#v", actual)
	}
	if !strings.Contains(actual.LastError, "intentional stderr tail") {
		t.Fatalf("stderr tail missing from last_error: %#v", actual.LastError)
	}
	events := sup.DrainEvents()
	if !hasEventType(events, "process_exited") || !hasEventType(events, "process_restart_scheduled") {
		t.Fatalf("expected exit and restart events: %#v", events)
	}

	stopped, err := sup.Stop(context.Background(), "rt-exit", time.Second)
	if err != nil {
		t.Fatalf("Stop() after exit error = %v", err)
	}
	if stopped.ActualState != agentruntime.ActualStateStopped || stopped.ProcessID != 0 {
		t.Fatalf("Stop() after exit should converge stopped: %#v", stopped)
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

func TestSupervisorPreparesCasterCommandWithLocalDefaults(t *testing.T) {
	root := t.TempDir()
	sup := NewProcessSupervisor(Options{
		CasterExecutable: "navcaster-caster-test",
		CasterWorkingDir: root,
		CasterListenHost: "127.0.0.2",
		CasterHealthHost: "127.0.0.3",
		CasterHealthPort: 19100,
		CasterRedisHost:  "127.0.0.4",
		CasterRedisPort:  6380,
		CasterEnv:        map[string]string{"NAVCASTER_TEST_ENV": "from-config"},
	})
	desired := agentruntime.DesiredState{
		RuntimeID:     "rt-caster",
		DesiredState:  agentruntime.DesiredStateRunning,
		ConfigVersion: 7,
		ListenPort:    4202,
		WorkerCount:   3,
		Version:       9,
	}

	prepared, command, args, err := sup.prepareCommand(desired)
	if err != nil {
		t.Fatalf("prepareCommand() error = %v", err)
	}
	if command != "navcaster-caster-test" || prepared.RuntimeKind != agentruntime.RuntimeKindCaster {
		t.Fatalf("unexpected command/kind: command=%s prepared=%#v", command, prepared)
	}
	joined := strings.Join(args, " ")
	for _, want := range []string{
		"--runtime-id rt-caster",
		"--listen-host 127.0.0.2",
		"--listen-port 4202",
		"--health-host 127.0.0.3",
		"--health-port 14202",
		"--worker-count 3",
		"--redis-host 127.0.0.4",
		"--redis-port 6380",
	} {
		if !strings.Contains(joined, want) {
			t.Fatalf("missing caster arg %q in %q", want, joined)
		}
	}
	if prepared.WorkingDir != filepath.Join(root, "runtimes", "rt-caster") {
		t.Fatalf("working dir default mismatch: %q", prepared.WorkingDir)
	}
	env := sup.buildEnv(prepared, "token-1")
	if !containsEnv(env, "NAVCASTER_RUNTIME_HEALTH_PORT=14202") || !containsEnv(env, "NAVCASTER_TEST_ENV=from-config") {
		t.Fatalf("expected health/env entries in %#v", env)
	}
}

func TestSupervisorRejectsCasterPortConflicts(t *testing.T) {
	sup := NewProcessSupervisor()
	sup.processes["rt-a"] = &processHandle{
		owned: true,
		cmd:   execCmdStub{pid: 1234}.cmd(),
		desired: agentruntime.DesiredState{
			RuntimeID:  "rt-a",
			ListenHost: "127.0.0.1",
			ListenPort: 4202,
			HealthHost: "127.0.0.1",
			HealthPort: 14202,
		},
		actual: agentruntime.ActualState{RuntimeID: "rt-a", ActualState: agentruntime.ActualStateRunning},
	}

	_, _, _, err := sup.prepareCommand(agentruntime.DesiredState{
		RuntimeID:     "rt-b",
		RuntimeKind:   agentruntime.RuntimeKindCaster,
		ListenHost:    "127.0.0.1",
		ListenPort:    4202,
		HealthHost:    "127.0.0.1",
		HealthPort:    14203,
		WorkerCount:   1,
		ConfigVersion: 1,
	})
	if err != nil {
		t.Fatalf("prepareCommand() error = %v", err)
	}
	err = sup.validateStart(agentruntime.DesiredState{
		RuntimeID:     "rt-b",
		RuntimeKind:   agentruntime.RuntimeKindCaster,
		ListenHost:    "127.0.0.1",
		ListenPort:    4202,
		HealthHost:    "127.0.0.1",
		HealthPort:    14203,
		WorkerCount:   1,
		ConfigVersion: 1,
	})
	if err == nil || !strings.Contains(err.Error(), "conflicts") {
		t.Fatalf("expected port conflict, got %v", err)
	}
}

func TestSupervisorRejectsWildcardCasterPortConflicts(t *testing.T) {
	sup := NewProcessSupervisor()
	sup.processes["rt-a"] = &processHandle{
		owned: true,
		cmd:   execCmdStub{pid: 1234}.cmd(),
		desired: agentruntime.DesiredState{
			RuntimeID:  "rt-a",
			ListenHost: "0.0.0.0",
			ListenPort: 4202,
			HealthHost: "127.0.0.1",
			HealthPort: 14202,
		},
		actual: agentruntime.ActualState{RuntimeID: "rt-a", ActualState: agentruntime.ActualStateRunning},
	}

	err := sup.validateStart(agentruntime.DesiredState{
		RuntimeID:   "rt-b",
		RuntimeKind: agentruntime.RuntimeKindCaster,
		ListenHost:  "127.0.0.1",
		ListenPort:  4202,
		HealthHost:  "127.0.0.1",
		HealthPort:  14203,
	})
	if err == nil || !strings.Contains(err.Error(), "conflicts") {
		t.Fatalf("expected wildcard listen conflict, got %v", err)
	}

	err = sup.validateStart(agentruntime.DesiredState{
		RuntimeID:   "rt-b",
		RuntimeKind: agentruntime.RuntimeKindCaster,
		ListenHost:  "127.0.0.1",
		ListenPort:  4203,
		HealthHost:  "0.0.0.0",
		HealthPort:  14202,
	})
	if err == nil || !strings.Contains(err.Error(), "conflicts") {
		t.Fatalf("expected wildcard health conflict, got %v", err)
	}

	err = sup.validateStart(agentruntime.DesiredState{
		RuntimeID:   "rt-b",
		RuntimeKind: agentruntime.RuntimeKindCaster,
		ListenHost:  "127.0.0.1",
		ListenPort:  14202,
		HealthHost:  "127.0.0.1",
		HealthPort:  14203,
	})
	if err == nil || !strings.Contains(err.Error(), "conflicts") {
		t.Fatalf("expected cross listen/health conflict, got %v", err)
	}
}

func TestSupervisorRefreshMapsCasterHealthAndMetrics(t *testing.T) {
	var serverURL string
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/health":
			fmt.Fprint(w, `{"ok":true,"runtime_id":"rt-probe"}`)
		case "/metrics":
			fmt.Fprint(w, `{"runtime_id":"rt-probe","running":true,"worker_count":2,"mount_count":2,"workers":[{"active_sessions":3,"active_mounts":1,"source_count":1,"client_count":2,"bytes_in":100,"bytes_out":250,"redis_connected":true,"redis_contexts_reserved":2},{"active_sessions":1,"active_mounts":1,"source_count":0,"client_count":1,"bytes_in":50,"bytes_out":150,"redis_connected":true,"redis_contexts_reserved":2}]}`)
		default:
			http.NotFound(w, r)
		}
	})
	server := httptest.NewServer(handler)
	defer server.Close()
	serverURL = server.URL
	host, port := splitHostPort(t, serverURL)

	sup := NewProcessSupervisor()
	handle := &processHandle{
		owned: true,
		cmd:   execCmdStub{pid: 1234}.cmd(),
		desired: agentruntime.DesiredState{
			RuntimeID:  "rt-probe",
			HealthHost: host,
			HealthPort: port,
		},
		actual: agentruntime.ActualState{
			RuntimeID:   "rt-probe",
			ActualState: agentruntime.ActualStateStarting,
			ProcessID:   1234,
			StartedAt:   time.Now().UTC().Add(-time.Second),
			UpdatedAt:   time.Now().UTC(),
		},
	}
	sup.processes["rt-probe"] = handle

	sup.Refresh(context.Background(), time.Second)
	actual, ok := sup.Actual("rt-probe")
	if !ok {
		t.Fatal("missing probed runtime")
	}
	if actual.ActualState != agentruntime.ActualStateRunning || actual.Connections != 4 || actual.Sources != 1 || actual.Clients != 3 || actual.Mounts != 2 {
		t.Fatalf("unexpected probed actual: %#v", actual)
	}
	if !actual.RedisConnected {
		t.Fatalf("expected RedisConnected from runtime metrics: %#v", actual)
	}
}

func TestSupervisorRefreshEmitsHealthFailedAndRecovered(t *testing.T) {
	healthy := false
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/health":
			if healthy {
				fmt.Fprint(w, `{"ok":true,"runtime_id":"rt-probe"}`)
			} else {
				http.Error(w, `{"ok":false}`, http.StatusServiceUnavailable)
			}
		case "/metrics":
			fmt.Fprint(w, `{"runtime_id":"rt-probe","running":true,"worker_count":1,"mount_count":0,"workers":[{"redis_connected":true}]}`)
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()
	host, port := splitHostPort(t, server.URL)

	sup := NewProcessSupervisor(Options{StartupGrace: time.Millisecond})
	handle := &processHandle{
		owned: true,
		cmd:   execCmdStub{pid: 1234}.cmd(),
		desired: agentruntime.DesiredState{
			RuntimeID:  "rt-probe",
			HealthHost: host,
			HealthPort: port,
		},
		actual: agentruntime.ActualState{
			RuntimeID:   "rt-probe",
			ActualState: agentruntime.ActualStateStarting,
			ProcessID:   1234,
			StartedAt:   time.Now().UTC().Add(-time.Second),
			UpdatedAt:   time.Now().UTC(),
		},
	}
	sup.processes["rt-probe"] = handle

	sup.Refresh(context.Background(), time.Second)
	actual, _ := sup.Actual("rt-probe")
	if actual.ActualState != agentruntime.ActualStateFailed {
		t.Fatalf("expected failed after health error: %#v", actual)
	}
	if events := sup.DrainEvents(); !hasEventType(events, "runtime_health_failed") {
		t.Fatalf("missing health failed event: %#v", events)
	}

	healthy = true
	sup.Refresh(context.Background(), time.Second)
	actual, _ = sup.Actual("rt-probe")
	if actual.ActualState != agentruntime.ActualStateRunning || !actual.RedisConnected {
		t.Fatalf("expected recovered running actual: %#v", actual)
	}
	if events := sup.DrainEvents(); !hasEventType(events, "runtime_health_recovered") {
		t.Fatalf("missing health recovered event: %#v", events)
	}
}

type execCmdStub struct {
	pid int
}

func (s execCmdStub) cmd() *exec.Cmd {
	cmd := exec.Command(os.Args[0], "-test.run=TestHelperProcessNeverStarted")
	cmd.Process = &os.Process{Pid: s.pid}
	return cmd
}

func containsEnv(env []string, want string) bool {
	for _, item := range env {
		if item == want {
			return true
		}
	}
	return false
}

func splitHostPort(t *testing.T, raw string) (string, int) {
	t.Helper()
	hostPort := strings.TrimPrefix(raw, "http://")
	host, portText, err := net.SplitHostPort(hostPort)
	if err != nil {
		t.Fatalf("split test server address %q: %v", raw, err)
	}
	port, err := strconv.Atoi(portText)
	if err != nil {
		t.Fatalf("parse test server port %q: %v", portText, err)
	}
	return host, port
}

func hasEventType(events []agentruntime.Event, eventType string) bool {
	for _, event := range events {
		if event.Type == eventType {
			return true
		}
	}
	return false
}
