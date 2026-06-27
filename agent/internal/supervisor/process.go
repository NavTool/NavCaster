package supervisor

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"sync"
	"time"

	agentruntime "navcaster/agent/internal/runtime"
)

var ErrNotManaged = errors.New("runtime is not managed by this agent process")

type ProcessSupervisor struct {
	mu        sync.Mutex
	processes map[string]*processHandle
}

type processHandle struct {
	cmd    *exec.Cmd
	cancel context.CancelFunc
	done   chan error
	actual agentruntime.ActualState
	exited bool
	owned  bool
}

func NewProcessSupervisor() *ProcessSupervisor {
	return &ProcessSupervisor{processes: make(map[string]*processHandle)}
}

func (s *ProcessSupervisor) Actual(runtimeID string) (agentruntime.ActualState, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	handle, ok := s.processes[runtimeID]
	if !ok {
		return agentruntime.ActualState{}, false
	}
	return handle.actual, true
}

// ObserveCachedActual seeds a last-known actual snapshot after Agent restart.
// The supervisor may report it, but will not stop or restart it without an
// owned process handle from this Agent process.
func (s *ProcessSupervisor) ObserveCachedActual(actual agentruntime.ActualState) {
	if actual.RuntimeID == "" || !actual.IsRunning() {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if existing, ok := s.processes[actual.RuntimeID]; ok && existing.owned && !existing.exited {
		return
	}
	s.processes[actual.RuntimeID] = &processHandle{
		actual: actual,
		owned:  false,
	}
}

func (s *ProcessSupervisor) Start(ctx context.Context, desired agentruntime.DesiredState) (agentruntime.ActualState, error) {
	if err := ctx.Err(); err != nil {
		return agentruntime.ActualState{}, err
	}
	if desired.RuntimeID == "" {
		return agentruntime.ActualState{}, fmt.Errorf("runtime_id is required")
	}

	command, args := desired.Command, desired.Args
	if command == "" || desired.RuntimeKind == agentruntime.RuntimeKindDummy {
		command, args = DummyCommand()
	}
	if command == "" {
		return agentruntime.ActualState{}, fmt.Errorf("runtime command is required")
	}

	s.mu.Lock()
	if existing, ok := s.processes[desired.RuntimeID]; ok && !existing.exited {
		actual := existing.actual
		s.mu.Unlock()
		return actual, fmt.Errorf("runtime %s is already managed", desired.RuntimeID)
	}
	s.mu.Unlock()

	startToken, err := newStartToken()
	if err != nil {
		return agentruntime.ActualState{}, err
	}
	childCtx, cancel := context.WithCancel(context.Background())
	cmd := exec.CommandContext(childCtx, command, args...)
	cmd.Dir = desired.WorkingDir
	cmd.Env = buildEnv(desired, startToken)

	if err := cmd.Start(); err != nil {
		cancel()
		return agentruntime.ActualState{
			RuntimeID:              desired.RuntimeID,
			HostID:                 desired.HostID,
			ActualState:            agentruntime.ActualStateFailed,
			LastError:              err.Error(),
			ObservedDesiredVersion: desired.Version,
			UpdatedAt:              time.Now().UTC(),
		}, fmt.Errorf("start runtime %s: %w", desired.RuntimeID, err)
	}

	now := time.Now().UTC()
	actual := agentruntime.ActualState{
		RuntimeID:              desired.RuntimeID,
		HostID:                 desired.HostID,
		ActualState:            agentruntime.ActualStateRunning,
		ProcessID:              cmd.Process.Pid,
		StartToken:             startToken,
		ConfigVersion:          desired.ConfigVersion,
		ConfigPath:             desired.ConfigPath,
		ConfigChecksum:         desired.ConfigChecksum,
		ListenPort:             desired.ListenPort,
		WorkerCount:            desired.WorkerCount,
		ObservedDesiredVersion: desired.Version,
		StartedAt:              now,
		UpdatedAt:              now,
	}
	handle := &processHandle{
		cmd:    cmd,
		cancel: cancel,
		done:   make(chan error, 1),
		actual: actual,
		owned:  true,
	}

	s.mu.Lock()
	s.processes[desired.RuntimeID] = handle
	s.mu.Unlock()

	go s.waitForExit(desired.RuntimeID, handle)
	return actual, nil
}

func (s *ProcessSupervisor) Stop(ctx context.Context, runtimeID string, timeout time.Duration) (agentruntime.ActualState, error) {
	if timeout <= 0 {
		timeout = 5 * time.Second
	}
	s.mu.Lock()
	handle, ok := s.processes[runtimeID]
	if !ok {
		s.mu.Unlock()
		return agentruntime.ActualState{RuntimeID: runtimeID, ActualState: agentruntime.ActualStateMissing, UpdatedAt: time.Now().UTC()}, ErrNotManaged
	}
	if handle.exited {
		actual := handle.actual
		delete(s.processes, runtimeID)
		s.mu.Unlock()
		return actual, nil
	}
	if !handle.owned || handle.cmd == nil || handle.cmd.Process == nil {
		actual := handle.actual
		s.mu.Unlock()
		return actual, ErrNotManaged
	}
	handle.actual.ActualState = agentruntime.ActualStateStopping
	handle.actual.UpdatedAt = time.Now().UTC()
	handle.cancel()
	s.mu.Unlock()

	select {
	case <-ctx.Done():
		return agentruntime.ActualState{RuntimeID: runtimeID, ActualState: agentruntime.ActualStateFailed, LastError: ctx.Err().Error(), UpdatedAt: time.Now().UTC()}, ctx.Err()
	case <-time.After(timeout):
		_ = handle.cmd.Process.Kill()
		<-handle.done
	case <-handle.done:
	}

	now := time.Now().UTC()
	actual := handle.actual
	actual.ActualState = agentruntime.ActualStateStopped
	actual.ProcessID = 0
	actual.UpdatedAt = now

	s.mu.Lock()
	delete(s.processes, runtimeID)
	s.mu.Unlock()
	return actual, nil
}

func (s *ProcessSupervisor) Restart(ctx context.Context, desired agentruntime.DesiredState, timeout time.Duration) (agentruntime.ActualState, error) {
	_, err := s.Stop(ctx, desired.RuntimeID, timeout)
	if err != nil {
		return agentruntime.ActualState{RuntimeID: desired.RuntimeID, ActualState: agentruntime.ActualStateFailed, LastError: err.Error(), UpdatedAt: time.Now().UTC()}, err
	}
	return s.Start(ctx, desired)
}

func (s *ProcessSupervisor) waitForExit(runtimeID string, handle *processHandle) {
	err := handle.cmd.Wait()
	handle.done <- err

	now := time.Now().UTC()
	actual := handle.actual
	actual.ProcessID = 0
	actual.UpdatedAt = now
	if err != nil {
		actual.ActualState = agentruntime.ActualStateFailed
		actual.LastError = err.Error()
		if exitErr, ok := err.(*exec.ExitError); ok {
			code := exitErr.ExitCode()
			actual.LastExitCode = &code
		}
	} else {
		actual.ActualState = agentruntime.ActualStateStopped
		code := 0
		actual.LastExitCode = &code
	}

	s.mu.Lock()
	if current, ok := s.processes[runtimeID]; ok && current == handle {
		handle.actual = actual
		handle.exited = true
	}
	s.mu.Unlock()
}

func DummyCommand() (string, []string) {
	exe, err := os.Executable()
	if err != nil {
		return "", nil
	}
	return exe, []string{"dummy-runtime"}
}

func buildEnv(desired agentruntime.DesiredState, startToken string) []string {
	env := os.Environ()
	env = append(env,
		"NAVCASTER_AGENT_MANAGED=true",
		"NAVCASTER_RUNTIME_ID="+desired.RuntimeID,
		"NAVCASTER_RUNTIME_START_TOKEN="+startToken,
		"NAVCASTER_RUNTIME_CONFIG_VERSION="+strconv.Itoa(desired.ConfigVersion),
	)
	if desired.ConfigPath != "" {
		env = append(env, "NAVCASTER_RUNTIME_CONFIG="+desired.ConfigPath)
	}
	for key, value := range desired.Env {
		env = append(env, key+"="+value)
	}
	return env
}

func newStartToken() (string, error) {
	var data [16]byte
	if _, err := rand.Read(data[:]); err != nil {
		return "", fmt.Errorf("generate runtime start token: %w", err)
	}
	return hex.EncodeToString(data[:]), nil
}
