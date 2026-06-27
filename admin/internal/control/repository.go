package control

import (
	"fmt"
	"sort"
	"sync"
	"time"

	"navcaster-admin/internal/errorsx"
)

type Repository interface {
	ListHosts() ([]Host, error)
	GetHost(hostID string) (Host, error)
	UpsertHost(Host) (Host, error)
	ListRuntimes() ([]Runtime, error)
	GetRuntime(runtimeID string) (Runtime, error)
	CreateRuntime(RuntimeCreateRequest) (Runtime, error)
	UpdateDesiredState(runtimeID string, req DesiredUpdateRequest) (Runtime, error)
	RecordActionIntent(runtimeID string, kind ActionKind, payload map[string]any) (ActionIntent, Runtime, error)
	ListDesiredStatesForHost(hostID string, sinceVersion int64) ([]DesiredRuntime, error)
	ApplyHeartbeat(hostID string, agentID string, at time.Time) error
	ApplyActualSnapshots(agentID string, hostID string, snapshots []ActualSnapshot) error
	RecordRuntimeEvents(agentID string, hostID string, events []RuntimeEvent) error
}

type MemoryRepository struct {
	mu        sync.Mutex
	hosts     map[string]Host
	runtimes  map[string]Runtime
	events    []RuntimeEvent
	nextID    int64
	version   int64
	actionSeq int64
}

func NewMemoryRepository() *MemoryRepository {
	now := time.Now().UTC()
	return &MemoryRepository{
		hosts: map[string]Host{
			"host_local": {
				ID:          "host_local",
				DisplayName: "Local development host",
				Status:      "registered",
				CreatedAt:   now,
				UpdatedAt:   now,
			},
		},
		runtimes: map[string]Runtime{},
		nextID:   1,
		version:  1,
	}
}

func (r *MemoryRepository) ListHosts() ([]Host, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	hosts := make([]Host, 0, len(r.hosts))
	for _, host := range r.hosts {
		hosts = append(hosts, host)
	}
	sort.Slice(hosts, func(i, j int) bool { return hosts[i].ID < hosts[j].ID })
	return hosts, nil
}

func (r *MemoryRepository) GetHost(hostID string) (Host, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	host, ok := r.hosts[hostID]
	if !ok {
		return Host{}, errorsx.NotFound("host not found")
	}
	return host, nil
}

func (r *MemoryRepository) UpsertHost(host Host) (Host, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := time.Now().UTC()
	if host.ID == "" {
		host.ID = newID("host", r.nextID)
		r.nextID++
	}
	existing, ok := r.hosts[host.ID]
	if ok {
		host.CreatedAt = existing.CreatedAt
	} else if host.CreatedAt.IsZero() {
		host.CreatedAt = now
	}
	if host.DisplayName == "" {
		host.DisplayName = host.ID
	}
	if host.Status == "" {
		host.Status = "registered"
	}
	host.UpdatedAt = now
	r.hosts[host.ID] = host
	return host, nil
}

func (r *MemoryRepository) ListRuntimes() ([]Runtime, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	runtimes := make([]Runtime, 0, len(r.runtimes))
	for _, runtime := range r.runtimes {
		runtimes = append(runtimes, cloneRuntime(runtime))
	}
	sort.Slice(runtimes, func(i, j int) bool { return runtimes[i].RuntimeID < runtimes[j].RuntimeID })
	return runtimes, nil
}

func (r *MemoryRepository) GetRuntime(runtimeID string) (Runtime, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	runtime, ok := r.runtimes[runtimeID]
	if !ok {
		return Runtime{}, errorsx.NotFound("runtime not found")
	}
	return cloneRuntime(runtime), nil
}

func (r *MemoryRepository) CreateRuntime(req RuntimeCreateRequest) (Runtime, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if req.HostID == "" {
		return Runtime{}, errorsx.BadRequest("host_id is required")
	}
	if _, ok := r.hosts[req.HostID]; !ok {
		return Runtime{}, errorsx.NotFound("host not found")
	}
	if req.DesiredState == "" {
		if req.StartImmediately {
			req.DesiredState = DesiredStateRunning
		} else {
			req.DesiredState = DesiredStateStopped
		}
	}
	if err := validateDesired(req.DesiredState, req.ListenPort, req.WorkerCount, req.MaxWorkers, req.RestartPolicy); err != nil {
		return Runtime{}, err
	}
	now := time.Now().UTC()
	runtimeID := newID("rt", r.nextID)
	r.nextID++
	r.version++
	desired := &DesiredRuntime{
		RuntimeID:     runtimeID,
		HostID:        req.HostID,
		DesiredState:  req.DesiredState,
		ConfigVersion: req.ConfigVersion,
		ListenPort:    req.ListenPort,
		WorkerCount:   req.WorkerCount,
		MaxWorkers:    req.MaxWorkers,
		RestartPolicy: req.RestartPolicy,
		Draining:      req.DesiredState == DesiredStateDraining,
		Version:       r.version,
		Generation:    r.version,
		UpdatedAt:     now,
	}
	runtime := Runtime{
		RuntimeID: runtimeID,
		HostID:    req.HostID,
		Name:      firstNonEmpty(req.Name, runtimeID),
		Desired:   desired,
		CreatedAt: now,
		UpdatedAt: now,
	}
	r.runtimes[runtimeID] = runtime
	return cloneRuntime(runtime), nil
}

func (r *MemoryRepository) UpdateDesiredState(runtimeID string, req DesiredUpdateRequest) (Runtime, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	runtime, ok := r.runtimes[runtimeID]
	if !ok {
		return Runtime{}, errorsx.NotFound("runtime not found")
	}
	if runtime.Desired == nil {
		return Runtime{}, errorsx.Conflict("runtime has no desired state")
	}
	desired := *runtime.Desired
	if req.DesiredState != "" {
		desired.DesiredState = req.DesiredState
	}
	if req.ConfigVersion != nil {
		desired.ConfigVersion = *req.ConfigVersion
	}
	if req.ListenPort != nil {
		desired.ListenPort = *req.ListenPort
	}
	if req.WorkerCount != nil {
		desired.WorkerCount = *req.WorkerCount
	}
	if req.MaxWorkers != nil {
		desired.MaxWorkers = *req.MaxWorkers
	}
	if req.RestartPolicy != "" {
		desired.RestartPolicy = req.RestartPolicy
	}
	if req.Draining != nil {
		desired.Draining = *req.Draining
	}
	if desired.DesiredState == DesiredStateDraining {
		desired.Draining = true
	}
	if err := validateDesired(desired.DesiredState, desired.ListenPort, desired.WorkerCount, desired.MaxWorkers, desired.RestartPolicy); err != nil {
		return Runtime{}, err
	}
	r.version++
	now := time.Now().UTC()
	desired.Version = r.version
	desired.Generation = r.version
	desired.UpdatedAt = now
	runtime.Desired = &desired
	runtime.UpdatedAt = now
	r.runtimes[runtimeID] = runtime
	return cloneRuntime(runtime), nil
}

func (r *MemoryRepository) RecordActionIntent(runtimeID string, kind ActionKind, payload map[string]any) (ActionIntent, Runtime, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	runtime, ok := r.runtimes[runtimeID]
	if !ok {
		return ActionIntent{}, Runtime{}, errorsx.NotFound("runtime not found")
	}
	if kind == "" {
		return ActionIntent{}, Runtime{}, errorsx.BadRequest("action kind is required")
	}
	if runtime.Desired == nil {
		return ActionIntent{}, Runtime{}, errorsx.Conflict("runtime has no desired state")
	}
	desired := *runtime.Desired
	switch kind {
	case ActionKindStart:
		desired.DesiredState = DesiredStateRunning
		desired.Draining = false
	case ActionKindStop:
		desired.DesiredState = DesiredStateStopped
		desired.Draining = false
	case ActionKindDrain:
		desired.DesiredState = DesiredStateDraining
		desired.Draining = true
	case ActionKindUndrain:
		desired.DesiredState = DesiredStateRunning
		desired.Draining = false
	case ActionKindRestart:
		desired.DesiredState = DesiredStateRunning
	default:
		return ActionIntent{}, Runtime{}, errorsx.BadRequest("unsupported action kind")
	}
	r.actionSeq++
	r.version++
	now := time.Now().UTC()
	desired.Version = r.version
	desired.Generation = r.version
	desired.UpdatedAt = now
	runtime.Desired = &desired
	runtime.UpdatedAt = now
	r.runtimes[runtimeID] = runtime
	intent := ActionIntent{
		ID:             newID("intent", r.actionSeq),
		RuntimeID:      runtimeID,
		Kind:           kind,
		Status:         "accepted",
		DesiredVersion: desired.Version,
		Payload:        payload,
		CreatedAt:      now,
	}
	return intent, cloneRuntime(runtime), nil
}

func (r *MemoryRepository) ListDesiredStatesForHost(hostID string, sinceVersion int64) ([]DesiredRuntime, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	desiredStates := make([]DesiredRuntime, 0)
	for _, runtime := range r.runtimes {
		if runtime.HostID != hostID || runtime.Desired == nil {
			continue
		}
		if runtime.Desired.Version <= sinceVersion {
			continue
		}
		desiredStates = append(desiredStates, *runtime.Desired)
	}
	sort.Slice(desiredStates, func(i, j int) bool {
		if desiredStates[i].Version == desiredStates[j].Version {
			return desiredStates[i].RuntimeID < desiredStates[j].RuntimeID
		}
		return desiredStates[i].Version < desiredStates[j].Version
	})
	return desiredStates, nil
}

func (r *MemoryRepository) ApplyHeartbeat(hostID string, agentID string, at time.Time) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	host, ok := r.hosts[hostID]
	if !ok {
		host = Host{ID: hostID, DisplayName: hostID, CreatedAt: at}
	}
	host.AgentID = agentID
	host.Status = "online"
	host.LastHeartbeat = &at
	host.UpdatedAt = at
	r.hosts[hostID] = host
	return nil
}

func (r *MemoryRepository) ApplyActualSnapshots(agentID string, hostID string, snapshots []ActualSnapshot) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := time.Now().UTC()
	if _, ok := r.hosts[hostID]; !ok {
		r.hosts[hostID] = Host{ID: hostID, DisplayName: hostID, AgentID: agentID, Status: "online", CreatedAt: now, UpdatedAt: now}
	}
	for _, snapshot := range snapshots {
		if snapshot.RuntimeID == "" {
			continue
		}
		if err := validateIngestIdentity(snapshot.HostID, snapshot.AgentID, hostID, agentID); err != nil {
			return err
		}
		snapshot.HostID = hostID
		snapshot.AgentID = agentID
		if snapshot.UpdatedAt.IsZero() {
			snapshot.UpdatedAt = now
		}
		runtime := r.runtimes[snapshot.RuntimeID]
		if runtime.RuntimeID == "" {
			runtime = Runtime{
				RuntimeID: snapshot.RuntimeID,
				HostID:    snapshot.HostID,
				Name:      snapshot.RuntimeID,
				CreatedAt: now,
			}
		}
		runtime.HostID = snapshot.HostID
		runtime.Actual = &snapshot
		runtime.UpdatedAt = now
		r.runtimes[snapshot.RuntimeID] = runtime
	}
	return nil
}

func (r *MemoryRepository) RecordRuntimeEvents(agentID string, hostID string, events []RuntimeEvent) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	for _, event := range events {
		if event.RuntimeID == "" || event.Type == "" {
			continue
		}
		if err := validateIngestIdentity(event.HostID, event.AgentID, hostID, agentID); err != nil {
			return err
		}
		event.HostID = hostID
		event.AgentID = agentID
		if event.OccurredAt.IsZero() {
			event.OccurredAt = time.Now().UTC()
		}
		r.events = append(r.events, event)
	}
	return nil
}

func validateIngestIdentity(payloadHostID string, payloadAgentID string, requestHostID string, requestAgentID string) error {
	if payloadHostID != "" && payloadHostID != requestHostID {
		return errorsx.BadRequest("payload host_id must match request host_id")
	}
	if payloadAgentID != "" && payloadAgentID != requestAgentID {
		return errorsx.BadRequest("payload agent_id must match request agent_id")
	}
	return nil
}

func validateDesired(state DesiredState, listenPort int, workerCount int, maxWorkers int, policy RestartPolicy) error {
	switch state {
	case DesiredStateRunning, DesiredStateStopped, DesiredStateDraining, DesiredStateDeleted:
	default:
		return errorsx.BadRequest("desired_state must be running, stopped, draining, or deleted")
	}
	if listenPort <= 0 || listenPort > 65535 {
		return errorsx.BadRequest("listen_port must be within 1..65535")
	}
	if workerCount <= 0 {
		return errorsx.BadRequest("worker_count must be positive")
	}
	if maxWorkers <= 0 {
		return errorsx.BadRequest("max_worker_count must be positive")
	}
	if workerCount > maxWorkers {
		return errorsx.BadRequest("worker_count cannot exceed max_worker_count")
	}
	switch policy {
	case RestartPolicyNever, RestartPolicyOnFailure, RestartPolicyAlways:
		return nil
	case "":
		return errorsx.BadRequest("restart_policy is required")
	default:
		return errorsx.BadRequest("restart_policy must be never, on_failure, or always")
	}
}

func cloneRuntime(runtime Runtime) Runtime {
	if runtime.Desired != nil {
		desired := *runtime.Desired
		runtime.Desired = &desired
	}
	if runtime.Actual != nil {
		actual := *runtime.Actual
		runtime.Actual = &actual
	}
	return runtime
}

func newID(prefix string, value int64) string {
	return fmt.Sprintf("%s_%s_%04d", prefix, time.Now().UTC().Format("20060102"), value)
}

func firstNonEmpty(values ...string) string {
	for _, value := range values {
		if value != "" {
			return value
		}
	}
	return ""
}
