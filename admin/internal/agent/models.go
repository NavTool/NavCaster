package agent

import "time"

type RegisterRequest struct {
	BootstrapToken string         `json:"bootstrap_token,omitempty"`
	AgentID        string         `json:"agent_id,omitempty"`
	HostID         string         `json:"host_id,omitempty"`
	Hostname       string         `json:"hostname"`
	MachineID      string         `json:"machine_id,omitempty"`
	OS             string         `json:"os,omitempty"`
	Arch           string         `json:"arch,omitempty"`
	AgentVersion   string         `json:"agent_version,omitempty"`
	Labels         map[string]any `json:"labels,omitempty"`
}

type RegisterResponse struct {
	AgentID             string `json:"agent_id"`
	AgentSecret         string `json:"agent_secret"`
	HostID              string `json:"host_id"`
	HeartbeatIntervalMS int    `json:"heartbeat_interval_ms"`
}

type HeartbeatRequest struct {
	AgentID          string         `json:"agent_id"`
	HostID           string         `json:"host_id"`
	Sequence         int64          `json:"sequence,omitempty"`
	Resources        map[string]any `json:"resources,omitempty"`
	RuntimeSummaries []RuntimeBeat  `json:"runtime_summaries,omitempty"`
}

type RuntimeBeat struct {
	RuntimeID              string `json:"runtime_id"`
	ActualState            string `json:"actual_state"`
	ProcessID              int    `json:"process_id,omitempty"`
	ObservedDesiredVersion int64  `json:"observed_desired_version,omitempty"`
}

type HeartbeatResponse struct {
	Accepted                bool      `json:"accepted"`
	ServerTime              time.Time `json:"server_time"`
	NextHeartbeatIntervalMS int       `json:"next_heartbeat_interval_ms"`
}

type DesiredStateResponse struct {
	Version  int64          `json:"version"`
	Runtimes []DesiredState `json:"runtimes"`
}

type DesiredState struct {
	RuntimeID     string `json:"runtime_id"`
	HostID        string `json:"host_id"`
	DesiredState  string `json:"desired_state"`
	ConfigVersion int64  `json:"config_version"`
	ListenPort    int    `json:"listen_port"`
	WorkerCount   int    `json:"worker_count"`
	MaxWorkers    int    `json:"max_worker_count"`
	RestartPolicy string `json:"restart_policy"`
	Draining      bool   `json:"draining"`
	Version       int64  `json:"version"`
	Generation    int64  `json:"generation"`
	UpdatedAt     string `json:"updated_at"`
}
