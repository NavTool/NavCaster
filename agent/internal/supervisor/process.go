package supervisor

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"net/http"
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
	opts      Options
	client    *http.Client
	processes map[string]*processHandle
	events    []agentruntime.Event
}

type processHandle struct {
	cmd            *exec.Cmd
	cancel         context.CancelFunc
	done           chan error
	desired        agentruntime.DesiredState
	actual         agentruntime.ActualState
	exited         bool
	owned          bool
	stopRequested  bool
	lastProbeBytes probeBytes
}

type Options struct {
	CasterExecutable string
	CasterWorkingDir string
	CasterListenHost string
	CasterHealthHost string
	CasterHealthPort int
	CasterRedisHost  string
	CasterRedisPort  int
	CasterEnv        map[string]string
	StartupGrace     time.Duration
	HTTPClient       *http.Client
}

type probeBytes struct {
	recv      int64
	send      int64
	updatedAt time.Time
}

func NewProcessSupervisor(options ...Options) *ProcessSupervisor {
	var opts Options
	if len(options) > 0 {
		opts = options[0]
	}
	if opts.CasterExecutable == "" {
		opts.CasterExecutable = "navcaster-caster"
	}
	if opts.CasterListenHost == "" {
		opts.CasterListenHost = "127.0.0.1"
	}
	if opts.CasterHealthHost == "" {
		opts.CasterHealthHost = "127.0.0.1"
	}
	if opts.CasterHealthPort <= 0 {
		opts.CasterHealthPort = 19000
	}
	if opts.CasterRedisPort <= 0 {
		opts.CasterRedisPort = 6379
	}
	if opts.StartupGrace <= 0 {
		opts.StartupGrace = 3 * time.Second
	}
	client := opts.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 2 * time.Second}
	}
	return &ProcessSupervisor{opts: opts, client: client, processes: make(map[string]*processHandle)}
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

	prepared, command, args, err := s.prepareCommand(desired)
	if err != nil {
		return agentruntime.ActualState{
			RuntimeID:              desired.RuntimeID,
			HostID:                 desired.HostID,
			ActualState:            agentruntime.ActualStateFailed,
			LastError:              err.Error(),
			ObservedDesiredVersion: desired.Version,
			UpdatedAt:              time.Now().UTC(),
		}, err
	}

	s.mu.Lock()
	if existing, ok := s.processes[prepared.RuntimeID]; ok && !existing.exited {
		actual := existing.actual
		s.mu.Unlock()
		return actual, fmt.Errorf("runtime %s is already managed", prepared.RuntimeID)
	}
	s.mu.Unlock()

	startToken, err := newStartToken()
	if err != nil {
		return agentruntime.ActualState{}, err
	}
	cmd := exec.Command(command, args...)
	cmd.Dir = prepared.WorkingDir
	cmd.Env = s.buildEnv(prepared, startToken)
	cancel := func() {
		if cmd.Process != nil {
			_ = cmd.Process.Signal(os.Interrupt)
		}
	}

	if err := cmd.Start(); err != nil {
		cancel()
		return agentruntime.ActualState{
			RuntimeID:              prepared.RuntimeID,
			HostID:                 prepared.HostID,
			ActualState:            agentruntime.ActualStateFailed,
			LastError:              err.Error(),
			ObservedDesiredVersion: prepared.Version,
			UpdatedAt:              time.Now().UTC(),
		}, fmt.Errorf("start runtime %s: %w", prepared.RuntimeID, err)
	}

	now := time.Now().UTC()
	actualState := agentruntime.ActualStateRunning
	if prepared.RuntimeKind == agentruntime.RuntimeKindCaster && prepared.HealthPort > 0 {
		actualState = agentruntime.ActualStateStarting
	}
	actual := agentruntime.ActualState{
		RuntimeID:              prepared.RuntimeID,
		HostID:                 prepared.HostID,
		ActualState:            actualState,
		ProcessID:              cmd.Process.Pid,
		StartToken:             startToken,
		ConfigVersion:          prepared.ConfigVersion,
		ConfigPath:             prepared.ConfigPath,
		ConfigChecksum:         prepared.ConfigChecksum,
		ListenPort:             prepared.ListenPort,
		WorkerCount:            prepared.WorkerCount,
		ObservedDesiredVersion: prepared.Version,
		StartedAt:              now,
		UpdatedAt:              now,
	}
	handle := &processHandle{
		cmd:     cmd,
		cancel:  cancel,
		done:    make(chan error, 1),
		desired: prepared,
		actual:  actual,
		owned:   true,
	}

	s.mu.Lock()
	s.processes[prepared.RuntimeID] = handle
	s.events = append(s.events, newEvent(prepared, "process_started", "info", cmd.Process.Pid, "runtime process started", nil))
	s.mu.Unlock()

	go s.waitForExit(prepared.RuntimeID, handle)
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
	handle.stopRequested = true
	handle.actual.ActualState = agentruntime.ActualStateStopping
	handle.actual.UpdatedAt = time.Now().UTC()
	handle.cancel()
	s.mu.Unlock()

	select {
	case <-ctx.Done():
		return agentruntime.ActualState{RuntimeID: runtimeID, ActualState: agentruntime.ActualStateFailed, LastError: ctx.Err().Error(), UpdatedAt: time.Now().UTC()}, ctx.Err()
	case <-time.After(timeout):
		s.appendRuntimeEvent(runtimeID, "process_stop_timeout", "error", "runtime stop timed out; killing process", nil)
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
	s.events = append(s.events, newEvent(handle.desired, "process_stopped", "info", 0, "runtime process stopped", nil))
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

	now := time.Now().UTC()
	actual := handle.actual
	actual.ProcessID = 0
	actual.UpdatedAt = now
	if handle.stopRequested {
		actual.ActualState = agentruntime.ActualStateStopped
		if err != nil {
			if exitErr, ok := err.(*exec.ExitError); ok {
				code := exitErr.ExitCode()
				actual.LastExitCode = &code
			}
		} else {
			code := 0
			actual.LastExitCode = &code
		}
	} else if err != nil {
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
		severity := "info"
		message := "runtime process exited"
		if actual.ActualState == agentruntime.ActualStateFailed {
			severity = "error"
			message = actual.LastError
		}
		s.events = append(s.events, newEvent(handle.desired, "process_exited", severity, 0, message, map[string]string{
			"actual_state": string(actual.ActualState),
			"exit_code":    exitCodeString(actual.LastExitCode),
		}))
	}
	s.mu.Unlock()
	handle.done <- err
}

func (s *ProcessSupervisor) ActualStates() []agentruntime.ActualState {
	s.mu.Lock()
	defer s.mu.Unlock()
	actuals := make([]agentruntime.ActualState, 0, len(s.processes))
	for _, handle := range s.processes {
		if handle.actual.RuntimeID != "" {
			actuals = append(actuals, handle.actual)
		}
	}
	return actuals
}

func (s *ProcessSupervisor) DrainEvents() []agentruntime.Event {
	s.mu.Lock()
	defer s.mu.Unlock()
	if len(s.events) == 0 {
		return nil
	}
	events := append([]agentruntime.Event(nil), s.events...)
	s.events = nil
	return events
}

func (s *ProcessSupervisor) Refresh(ctx context.Context, timeout time.Duration) {
	handles := s.copyProbeTargets()
	for runtimeID, handle := range handles {
		if err := ctx.Err(); err != nil {
			s.markProbeFailed(runtimeID, handle, err, false)
			continue
		}
		actual, counters, err := s.probe(ctx, handle, timeout)
		if err != nil {
			s.markProbeFailed(runtimeID, handle, err, true)
			continue
		}
		s.mu.Lock()
		if current, ok := s.processes[runtimeID]; ok && current == handle && !current.exited {
			current.actual = actual
			current.lastProbeBytes = counters
		}
		s.mu.Unlock()
	}
}

func (s *ProcessSupervisor) copyProbeTargets() map[string]*processHandle {
	s.mu.Lock()
	defer s.mu.Unlock()
	handles := make(map[string]*processHandle)
	for runtimeID, handle := range s.processes {
		if handle.owned && !handle.exited && handle.cmd != nil && handle.cmd.Process != nil && handle.desired.HealthPort > 0 {
			handles[runtimeID] = handle
		}
	}
	return handles
}

func (s *ProcessSupervisor) probe(ctx context.Context, handle *processHandle, timeout time.Duration) (agentruntime.ActualState, probeBytes, error) {
	if timeout <= 0 {
		timeout = 2 * time.Second
	}
	probeCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	health, err := s.getHealth(probeCtx, handle.desired)
	if err != nil {
		return agentruntime.ActualState{}, probeBytes{}, err
	}
	probeCtx, cancel = context.WithTimeout(ctx, timeout)
	defer cancel()
	metrics, err := s.getMetrics(probeCtx, handle.desired)
	if err != nil {
		return agentruntime.ActualState{}, probeBytes{}, err
	}

	now := time.Now().UTC()
	actual := handle.actual
	actual.ActualState = agentruntime.ActualStateRunning
	if !health.OK || !metrics.Running {
		actual.ActualState = agentruntime.ActualStateFailed
		actual.LastError = "runtime health endpoint is not ready"
	} else {
		actual.LastError = ""
	}
	actual.WorkerCount = firstPositiveInt(metrics.WorkerCount, handle.desired.WorkerCount)
	actual.Mounts = int(metrics.MountCount)
	actual.Sources = metrics.SumSources()
	actual.Clients = metrics.SumClients()
	actual.Connections = metrics.SumSessions()
	actual.RecvBPS, actual.SendBPS = computeBPS(handle.lastProbeBytes, metrics.SumBytesIn(), metrics.SumBytesOut(), now)
	actual.RedisConnected = metrics.HasRedisContexts()
	actual.UpdatedAt = now
	return actual, probeBytes{recv: metrics.SumBytesIn(), send: metrics.SumBytesOut(), updatedAt: now}, nil
}

func (s *ProcessSupervisor) getHealth(ctx context.Context, desired agentruntime.DesiredState) (healthResponse, error) {
	var health healthResponse
	status, err := s.getJSON(ctx, desired, "/health", &health)
	if err != nil {
		return healthResponse{}, err
	}
	if status < 200 || status >= 300 {
		return healthResponse{}, fmt.Errorf("runtime health returned status %d", status)
	}
	return health, nil
}

func (s *ProcessSupervisor) getMetrics(ctx context.Context, desired agentruntime.DesiredState) (metricsResponse, error) {
	var metrics metricsResponse
	status, err := s.getJSON(ctx, desired, "/metrics", &metrics)
	if err != nil {
		return metricsResponse{}, err
	}
	if status < 200 || status >= 300 {
		return metricsResponse{}, fmt.Errorf("runtime metrics returned status %d", status)
	}
	return metrics, nil
}

func (s *ProcessSupervisor) getJSON(ctx context.Context, desired agentruntime.DesiredState, path string, out any) (int, error) {
	target := "http://" + desired.HealthHost + ":" + strconv.Itoa(desired.HealthPort) + path
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, target, nil)
	if err != nil {
		return 0, err
	}
	req.Header.Set("Accept", "application/json")
	resp, err := s.client.Do(req)
	if err != nil {
		return 0, err
	}
	defer resp.Body.Close()
	data, err := io.ReadAll(io.LimitReader(resp.Body, 1024*1024))
	if err != nil {
		return resp.StatusCode, err
	}
	if len(data) > 0 && out != nil {
		if err := json.Unmarshal(data, out); err != nil {
			return resp.StatusCode, err
		}
	}
	return resp.StatusCode, nil
}

func (s *ProcessSupervisor) markProbeFailed(runtimeID string, handle *processHandle, err error, allowGrace bool) {
	now := time.Now().UTC()
	s.mu.Lock()
	defer s.mu.Unlock()
	current, ok := s.processes[runtimeID]
	if !ok || current != handle || current.exited {
		return
	}
	actual := current.actual
	if allowGrace && !actual.StartedAt.IsZero() && now.Sub(actual.StartedAt) < s.opts.StartupGrace {
		actual.ActualState = agentruntime.ActualStateStarting
	} else {
		emitEvent := actual.ActualState != agentruntime.ActualStateFailed
		actual.ActualState = agentruntime.ActualStateFailed
		if emitEvent {
			s.events = append(s.events, newEvent(current.desired, "runtime_health_failed", "error", actual.ProcessID, err.Error(), nil))
		}
	}
	actual.LastError = err.Error()
	actual.UpdatedAt = now
	current.actual = actual
}

type healthResponse struct {
	OK        bool   `json:"ok"`
	RuntimeID string `json:"runtime_id"`
}

type metricsResponse struct {
	RuntimeID   string          `json:"runtime_id"`
	Running     bool            `json:"running"`
	WorkerCount int             `json:"worker_count"`
	MountCount  int             `json:"mount_count"`
	Workers     []workerMetrics `json:"workers"`
}

type workerMetrics struct {
	ActiveSessions        int   `json:"active_sessions"`
	ActiveMounts          int   `json:"active_mounts"`
	SourceCount           int   `json:"source_count"`
	ClientCount           int   `json:"client_count"`
	BytesIn               int64 `json:"bytes_in"`
	BytesOut              int64 `json:"bytes_out"`
	RedisContextsReserved int64 `json:"redis_contexts_reserved"`
}

func (m metricsResponse) SumSessions() int {
	total := 0
	for _, worker := range m.Workers {
		total += worker.ActiveSessions
	}
	return total
}

func (m metricsResponse) SumSources() int {
	total := 0
	for _, worker := range m.Workers {
		total += worker.SourceCount
	}
	return total
}

func (m metricsResponse) SumClients() int {
	total := 0
	for _, worker := range m.Workers {
		total += worker.ClientCount
	}
	return total
}

func (m metricsResponse) SumBytesIn() int64 {
	var total int64
	for _, worker := range m.Workers {
		total += worker.BytesIn
	}
	return total
}

func (m metricsResponse) SumBytesOut() int64 {
	var total int64
	for _, worker := range m.Workers {
		total += worker.BytesOut
	}
	return total
}

func (m metricsResponse) HasRedisContexts() bool {
	for _, worker := range m.Workers {
		if worker.RedisContextsReserved > 0 {
			return true
		}
	}
	return false
}

func (s *ProcessSupervisor) prepareCommand(desired agentruntime.DesiredState) (agentruntime.DesiredState, string, []string, error) {
	prepared := s.prepareDesiredDefaults(desired)
	switch {
	case prepared.RuntimeKind == agentruntime.RuntimeKindDummy:
		command, args := DummyCommand()
		if command == "" {
			return agentruntime.DesiredState{}, "", nil, fmt.Errorf("dummy runtime command is unavailable")
		}
		return prepared, command, args, nil
	case prepared.RuntimeKind == agentruntime.RuntimeKindCommand && prepared.Command == "":
		return agentruntime.DesiredState{}, "", nil, fmt.Errorf("runtime command is required")
	case prepared.Command != "":
		return prepared, prepared.Command, prepared.Args, nil
	default:
		prepared.RuntimeKind = agentruntime.RuntimeKindCaster
		args := []string{
			"--runtime-id", prepared.RuntimeID,
			"--listen-host", prepared.ListenHost,
			"--listen-port", strconv.Itoa(prepared.ListenPort),
			"--health-host", prepared.HealthHost,
			"--health-port", strconv.Itoa(prepared.HealthPort),
		}
		if prepared.WorkerCount > 0 {
			args = append(args, "--worker-count", strconv.Itoa(prepared.WorkerCount))
		}
		if s.opts.CasterRedisHost != "" {
			args = append(args, "--redis-host", s.opts.CasterRedisHost)
		}
		if s.opts.CasterRedisPort > 0 {
			args = append(args, "--redis-port", strconv.Itoa(s.opts.CasterRedisPort))
		}
		args = append(args, prepared.Args...)
		return prepared, s.opts.CasterExecutable, args, nil
	}
}

func (s *ProcessSupervisor) prepareDesiredDefaults(desired agentruntime.DesiredState) agentruntime.DesiredState {
	prepared := desired
	if prepared.RuntimeKind == "" {
		if prepared.Command != "" {
			prepared.RuntimeKind = agentruntime.RuntimeKindCommand
		} else {
			prepared.RuntimeKind = agentruntime.RuntimeKindCaster
		}
	}
	if prepared.WorkingDir == "" {
		prepared.WorkingDir = s.opts.CasterWorkingDir
	}
	if prepared.RuntimeKind == agentruntime.RuntimeKindCaster {
		if prepared.ListenHost == "" {
			prepared.ListenHost = s.opts.CasterListenHost
		}
		if prepared.HealthHost == "" {
			prepared.HealthHost = s.opts.CasterHealthHost
		}
		if prepared.HealthPort <= 0 {
			prepared.HealthPort = s.defaultHealthPort(prepared)
		}
	} else if prepared.HealthPort > 0 && prepared.HealthHost == "" {
		prepared.HealthHost = s.opts.CasterHealthHost
	}
	return prepared
}

func (s *ProcessSupervisor) defaultHealthPort(desired agentruntime.DesiredState) int {
	if desired.ListenPort > 0 && desired.ListenPort+10000 <= 65535 {
		return desired.ListenPort + 10000
	}
	base := s.opts.CasterHealthPort
	if base <= 0 {
		base = 19000
	}
	candidate := base + runtimeHashOffset(desired.RuntimeID)
	if candidate > 65535 {
		return 65535
	}
	return candidate
}

func DummyCommand() (string, []string) {
	exe, err := os.Executable()
	if err != nil {
		return "", nil
	}
	return exe, []string{"dummy-runtime"}
}

func (s *ProcessSupervisor) buildEnv(desired agentruntime.DesiredState, startToken string) []string {
	env := os.Environ()
	env = append(env,
		"NAVCASTER_AGENT_MANAGED=true",
		"NAVCASTER_RUNTIME_ID="+desired.RuntimeID,
		"NAVCASTER_RUNTIME_START_TOKEN="+startToken,
		"NAVCASTER_RUNTIME_CONFIG_VERSION="+strconv.Itoa(desired.ConfigVersion),
		"NAVCASTER_RUNTIME_LISTEN_HOST="+desired.ListenHost,
		"NAVCASTER_RUNTIME_LISTEN_PORT="+strconv.Itoa(desired.ListenPort),
		"NAVCASTER_RUNTIME_HEALTH_HOST="+desired.HealthHost,
		"NAVCASTER_RUNTIME_HEALTH_PORT="+strconv.Itoa(desired.HealthPort),
	)
	if desired.ConfigPath != "" {
		env = append(env, "NAVCASTER_RUNTIME_CONFIG="+desired.ConfigPath)
	}
	for key, value := range s.opts.CasterEnv {
		env = append(env, key+"="+value)
	}
	for key, value := range desired.Env {
		env = append(env, key+"="+value)
	}
	return env
}

func newEvent(desired agentruntime.DesiredState, eventType string, severity string, processID int, message string, metadata map[string]string) agentruntime.Event {
	return agentruntime.Event{
		RuntimeID:      desired.RuntimeID,
		HostID:         desired.HostID,
		Type:           eventType,
		Severity:       severity,
		DesiredVersion: desired.Version,
		ProcessID:      processID,
		Message:        message,
		OccurredAt:     time.Now().UTC(),
		Metadata:       metadata,
	}
}

func (s *ProcessSupervisor) appendRuntimeEvent(runtimeID string, eventType string, severity string, message string, metadata map[string]string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	handle, ok := s.processes[runtimeID]
	if !ok {
		return
	}
	s.events = append(s.events, newEvent(handle.desired, eventType, severity, handle.actual.ProcessID, message, metadata))
}

func exitCodeString(code *int) string {
	if code == nil {
		return ""
	}
	return strconv.Itoa(*code)
}

func runtimeHashOffset(runtimeID string) int {
	hash := fnv.New32a()
	_, _ = hash.Write([]byte(runtimeID))
	return int(hash.Sum32() % 1000)
}

func computeBPS(previous probeBytes, recvBytes int64, sendBytes int64, now time.Time) (int64, int64) {
	if previous.updatedAt.IsZero() || !now.After(previous.updatedAt) {
		return 0, 0
	}
	seconds := now.Sub(previous.updatedAt).Seconds()
	if seconds <= 0 {
		return 0, 0
	}
	recvDelta := recvBytes - previous.recv
	sendDelta := sendBytes - previous.send
	if recvDelta < 0 {
		recvDelta = 0
	}
	if sendDelta < 0 {
		sendDelta = 0
	}
	return int64(float64(recvDelta) / seconds), int64(float64(sendDelta) / seconds)
}

func firstPositiveInt(values ...int) int {
	for _, value := range values {
		if value > 0 {
			return value
		}
	}
	return 0
}

func newStartToken() (string, error) {
	var data [16]byte
	if _, err := rand.Read(data[:]); err != nil {
		return "", fmt.Errorf("generate runtime start token: %w", err)
	}
	return hex.EncodeToString(data[:]), nil
}
