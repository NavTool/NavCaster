package control

import "time"

type DesiredState string

const (
	DesiredStateRunning  DesiredState = "running"
	DesiredStateStopped  DesiredState = "stopped"
	DesiredStateDraining DesiredState = "draining"
	DesiredStateDeleted  DesiredState = "deleted"
)

type RestartPolicy string

const (
	RestartPolicyNever     RestartPolicy = "never"
	RestartPolicyOnFailure RestartPolicy = "on_failure"
	RestartPolicyAlways    RestartPolicy = "always"
)

type ActionKind string

const (
	ActionKindStart   ActionKind = "start"
	ActionKindStop    ActionKind = "stop"
	ActionKindRestart ActionKind = "restart"
	ActionKindDrain   ActionKind = "drain"
	ActionKindUndrain ActionKind = "undrain"
)

type Host struct {
	ID            string         `json:"host_id"`
	DisplayName   string         `json:"display_name"`
	AgentID       string         `json:"agent_id,omitempty"`
	Status        string         `json:"status"`
	Labels        map[string]any `json:"labels,omitempty"`
	LastHeartbeat *time.Time     `json:"last_heartbeat_at,omitempty"`
	CreatedAt     time.Time      `json:"created_at"`
	UpdatedAt     time.Time      `json:"updated_at"`
}

type DesiredRuntime struct {
	RuntimeID     string        `json:"runtime_id"`
	HostID        string        `json:"host_id"`
	DesiredState  DesiredState  `json:"desired_state"`
	ConfigVersion int64         `json:"config_version"`
	ListenPort    int           `json:"listen_port"`
	WorkerCount   int           `json:"worker_count"`
	MaxWorkers    int           `json:"max_worker_count"`
	RestartPolicy RestartPolicy `json:"restart_policy"`
	Draining      bool          `json:"draining"`
	Version       int64         `json:"version"`
	Generation    int64         `json:"generation"`
	UpdatedAt     time.Time     `json:"updated_at"`
}

type Runtime struct {
	RuntimeID string          `json:"runtime_id"`
	HostID    string          `json:"host_id"`
	Name      string          `json:"name"`
	Desired   *DesiredRuntime `json:"desired,omitempty"`
	Actual    *ActualSnapshot `json:"actual,omitempty"`
	CreatedAt time.Time       `json:"created_at"`
	UpdatedAt time.Time       `json:"updated_at"`
}

type ActualSnapshot struct {
	RuntimeID              string    `json:"runtime_id"`
	HostID                 string    `json:"host_id"`
	AgentID                string    `json:"agent_id,omitempty"`
	ActualState            string    `json:"actual_state"`
	ProcessID              int       `json:"process_id,omitempty"`
	StartToken             string    `json:"start_token,omitempty"`
	ConfigVersion          int64     `json:"config_version,omitempty"`
	ListenPort             int       `json:"listen_port,omitempty"`
	WorkerCount            int       `json:"worker_count,omitempty"`
	Connections            int       `json:"connections,omitempty"`
	Mounts                 int       `json:"mounts,omitempty"`
	Sources                int       `json:"sources,omitempty"`
	Clients                int       `json:"clients,omitempty"`
	SendBPS                int64     `json:"send_bps,omitempty"`
	RecvBPS                int64     `json:"recv_bps,omitempty"`
	LoopDelayP95MS         int       `json:"loop_delay_ms_p95,omitempty"`
	RedisConnected         bool      `json:"redis_connected,omitempty"`
	LastError              string    `json:"last_error,omitempty"`
	ObservedDesiredVersion int64     `json:"observed_desired_version,omitempty"`
	UpdatedAt              time.Time `json:"updated_at"`
}

type ActionIntent struct {
	ID             string         `json:"intent_id"`
	RuntimeID      string         `json:"runtime_id"`
	Kind           ActionKind     `json:"kind"`
	Status         string         `json:"status"`
	DesiredVersion int64          `json:"desired_version"`
	Payload        map[string]any `json:"payload,omitempty"`
	CreatedAt      time.Time      `json:"created_at"`
}

type RuntimeCreateRequest struct {
	HostID           string        `json:"host_id"`
	Name             string        `json:"name"`
	DesiredState     DesiredState  `json:"desired_state"`
	ConfigVersion    int64         `json:"config_version"`
	ListenPort       int           `json:"listen_port"`
	WorkerCount      int           `json:"worker_count"`
	MaxWorkers       int           `json:"max_worker_count"`
	RestartPolicy    RestartPolicy `json:"restart_policy"`
	StartImmediately bool          `json:"start_immediately"`
}

type DesiredUpdateRequest struct {
	DesiredState  DesiredState  `json:"desired_state"`
	ConfigVersion *int64        `json:"config_version,omitempty"`
	ListenPort    *int          `json:"listen_port,omitempty"`
	WorkerCount   *int          `json:"worker_count,omitempty"`
	MaxWorkers    *int          `json:"max_worker_count,omitempty"`
	RestartPolicy RestartPolicy `json:"restart_policy,omitempty"`
	Draining      *bool         `json:"draining,omitempty"`
}
