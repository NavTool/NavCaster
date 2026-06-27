package agent

import (
	"crypto/rand"
	"encoding/hex"
	"time"

	"navcaster-admin/internal/config"
	"navcaster-admin/internal/control"
	"navcaster-admin/internal/errorsx"
)

type Service struct {
	cfg  config.Config
	repo control.Repository
}

func NewService(cfg config.Config, repo control.Repository) Service {
	return Service{cfg: cfg, repo: repo}
}

func (s Service) Register(req RegisterRequest) (RegisterResponse, error) {
	if s.cfg.BootstrapToken != "" && req.BootstrapToken != s.cfg.BootstrapToken {
		return RegisterResponse{}, errorsx.Unauthorized("invalid bootstrap token")
	}
	if req.Hostname == "" {
		return RegisterResponse{}, errorsx.BadRequest("hostname is required")
	}
	agentID := req.AgentID
	if agentID == "" {
		agentID = "ag_" + randomHex(8)
	}
	hostID := req.HostID
	if hostID == "" {
		hostID = "host_" + randomHex(8)
	}
	labels := req.Labels
	if labels == nil {
		labels = map[string]any{}
	}
	if req.MachineID != "" {
		labels["machine_id"] = req.MachineID
	}
	if req.OS != "" {
		labels["os"] = req.OS
	}
	if req.Arch != "" {
		labels["arch"] = req.Arch
	}
	if req.AgentVersion != "" {
		labels["agent_version"] = req.AgentVersion
	}
	host, err := s.repo.UpsertHost(control.Host{
		ID:          hostID,
		DisplayName: req.Hostname,
		AgentID:     agentID,
		Status:      "registered",
		Labels:      labels,
	})
	if err != nil {
		return RegisterResponse{}, err
	}
	return RegisterResponse{
		AgentID:             agentID,
		AgentSecret:         "secret_" + randomHex(16),
		HostID:              host.ID,
		HeartbeatIntervalMS: int(s.cfg.HeartbeatInterval.Milliseconds()),
	}, nil
}

func (s Service) Heartbeat(req HeartbeatRequest) (HeartbeatResponse, error) {
	if req.AgentID == "" {
		return HeartbeatResponse{}, errorsx.BadRequest("agent_id is required")
	}
	if req.HostID == "" {
		return HeartbeatResponse{}, errorsx.BadRequest("host_id is required")
	}
	now := time.Now().UTC()
	if err := s.repo.ApplyHeartbeat(req.HostID, req.AgentID, now); err != nil {
		return HeartbeatResponse{}, err
	}
	return HeartbeatResponse{
		Accepted:                true,
		ServerTime:              now,
		NextHeartbeatIntervalMS: int(s.cfg.HeartbeatInterval.Milliseconds()),
	}, nil
}

func (s Service) DesiredState(agentID string, sinceVersion int64) (DesiredStateResponse, error) {
	if agentID == "" {
		return DesiredStateResponse{}, errorsx.BadRequest("agent_id is required")
	}
	hosts, err := s.repo.ListHosts()
	if err != nil {
		return DesiredStateResponse{}, err
	}
	hostID := ""
	for _, host := range hosts {
		if host.AgentID == agentID {
			hostID = host.ID
			break
		}
	}
	if hostID == "" {
		return DesiredStateResponse{}, errorsx.NotFound("agent is not registered")
	}
	states, err := s.repo.ListDesiredStatesForHost(hostID, sinceVersion)
	if err != nil {
		return DesiredStateResponse{}, err
	}
	response := DesiredStateResponse{Version: sinceVersion, Runtimes: make([]DesiredState, 0, len(states))}
	for _, state := range states {
		if state.Version > response.Version {
			response.Version = state.Version
		}
		response.Runtimes = append(response.Runtimes, DesiredState{
			RuntimeID:     state.RuntimeID,
			HostID:        state.HostID,
			DesiredState:  string(state.DesiredState),
			ConfigVersion: state.ConfigVersion,
			ListenPort:    state.ListenPort,
			WorkerCount:   state.WorkerCount,
			MaxWorkers:    state.MaxWorkers,
			RestartPolicy: string(state.RestartPolicy),
			Draining:      state.Draining,
			Version:       state.Version,
			Generation:    state.Generation,
			UpdatedAt:     state.UpdatedAt.Format(time.RFC3339Nano),
		})
	}
	return response, nil
}

func randomHex(bytesLen int) string {
	buf := make([]byte, bytesLen)
	if _, err := rand.Read(buf); err != nil {
		return "local"
	}
	return hex.EncodeToString(buf)
}
