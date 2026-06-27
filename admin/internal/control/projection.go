package control

import "time"

type ProjectionPublisher interface {
	PublishRuntimeDesired(DesiredRuntime) error
	PublishHostDesiredState(hostID string, states []DesiredRuntime) error
	PublishRuntimeActual(ActualSnapshot) error
	PublishAgentHeartbeat(AgentHeartbeatProjection) error
}

type AgentHeartbeatProjection struct {
	AgentID    string    `json:"agent_id"`
	HostID     string    `json:"host_id"`
	ObservedAt time.Time `json:"observed_at"`
	ExpiresAt  time.Time `json:"expires_at,omitempty"`
	Service    string    `json:"service"`
	Projection string    `json:"projection"`
}

type ProjectingRepository struct {
	inner     Repository
	publisher ProjectionPublisher
}

func NewProjectingRepository(inner Repository, publisher ProjectionPublisher) *ProjectingRepository {
	return &ProjectingRepository{inner: inner, publisher: publisher}
}

func (r *ProjectingRepository) ListHosts() ([]Host, error) {
	return r.inner.ListHosts()
}

func (r *ProjectingRepository) GetHost(hostID string) (Host, error) {
	return r.inner.GetHost(hostID)
}

func (r *ProjectingRepository) UpsertHost(host Host) (Host, error) {
	return r.inner.UpsertHost(host)
}

func (r *ProjectingRepository) ListRuntimes() ([]Runtime, error) {
	return r.inner.ListRuntimes()
}

func (r *ProjectingRepository) GetRuntime(runtimeID string) (Runtime, error) {
	return r.inner.GetRuntime(runtimeID)
}

func (r *ProjectingRepository) CreateRuntime(req RuntimeCreateRequest) (Runtime, error) {
	runtime, err := r.inner.CreateRuntime(req)
	if err != nil {
		return Runtime{}, err
	}
	return runtime, r.publishRuntimeDesired(runtime)
}

func (r *ProjectingRepository) UpdateDesiredState(runtimeID string, req DesiredUpdateRequest) (Runtime, error) {
	runtime, err := r.inner.UpdateDesiredState(runtimeID, req)
	if err != nil {
		return Runtime{}, err
	}
	return runtime, r.publishRuntimeDesired(runtime)
}

func (r *ProjectingRepository) RecordActionIntent(runtimeID string, kind ActionKind, payload map[string]any) (ActionIntent, Runtime, error) {
	intent, runtime, err := r.inner.RecordActionIntent(runtimeID, kind, payload)
	if err != nil {
		return ActionIntent{}, Runtime{}, err
	}
	return intent, runtime, r.publishRuntimeDesired(runtime)
}

func (r *ProjectingRepository) ListDesiredStatesForHost(hostID string, sinceVersion int64) ([]DesiredRuntime, error) {
	return r.inner.ListDesiredStatesForHost(hostID, sinceVersion)
}

func (r *ProjectingRepository) ApplyHeartbeat(hostID string, agentID string, at time.Time) error {
	if err := r.inner.ApplyHeartbeat(hostID, agentID, at); err != nil {
		return err
	}
	if r.publisher == nil {
		return nil
	}
	return r.publisher.PublishAgentHeartbeat(AgentHeartbeatProjection{
		AgentID:    agentID,
		HostID:     hostID,
		ObservedAt: at,
		Service:    "navcaster-admin",
		Projection: "v2:agent:heartbeat",
	})
}

func (r *ProjectingRepository) ApplyActualSnapshots(agentID string, hostID string, snapshots []ActualSnapshot) error {
	if err := r.inner.ApplyActualSnapshots(agentID, hostID, snapshots); err != nil {
		return err
	}
	if r.publisher == nil {
		return nil
	}
	for _, snapshot := range snapshots {
		if snapshot.RuntimeID == "" {
			continue
		}
		if snapshot.AgentID == "" {
			snapshot.AgentID = agentID
		}
		if snapshot.HostID == "" {
			snapshot.HostID = hostID
		}
		if snapshot.UpdatedAt.IsZero() {
			snapshot.UpdatedAt = time.Now().UTC()
		}
		if err := r.publisher.PublishRuntimeActual(snapshot); err != nil {
			return err
		}
	}
	return nil
}

func (r *ProjectingRepository) RecordRuntimeEvents(agentID string, hostID string, events []RuntimeEvent) error {
	return r.inner.RecordRuntimeEvents(agentID, hostID, events)
}

func (r *ProjectingRepository) publishRuntimeDesired(runtime Runtime) error {
	if r.publisher == nil || runtime.Desired == nil {
		return nil
	}
	if err := r.publisher.PublishRuntimeDesired(*runtime.Desired); err != nil {
		return err
	}
	states, err := r.inner.ListDesiredStatesForHost(runtime.Desired.HostID, 0)
	if err != nil {
		return err
	}
	return r.publisher.PublishHostDesiredState(runtime.Desired.HostID, states)
}
