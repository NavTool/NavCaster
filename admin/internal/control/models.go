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

type ActionIntentStatus string

const (
	ActionIntentAccepted   ActionIntentStatus = "accepted"
	ActionIntentProjected  ActionIntentStatus = "projected"
	ActionIntentObserved   ActionIntentStatus = "observed"
	ActionIntentSuperseded ActionIntentStatus = "superseded"
	ActionIntentFailed     ActionIntentStatus = "failed"
)

type RuntimeControlStatus string

const (
	RuntimeControlConverged RuntimeControlStatus = "converged"
	RuntimeControlPending   RuntimeControlStatus = "pending"
	RuntimeControlFailed    RuntimeControlStatus = "failed"
	RuntimeControlStale     RuntimeControlStatus = "stale"
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
	Control   *RuntimeControl `json:"control,omitempty"`
	CreatedAt time.Time       `json:"created_at"`
	UpdatedAt time.Time       `json:"updated_at"`
}

type RuntimeControl struct {
	Status                 RuntimeControlStatus `json:"status"`
	DesiredVersion         int64                `json:"desired_version,omitempty"`
	ObservedDesiredVersion int64                `json:"observed_desired_version,omitempty"`
	DesiredState           DesiredState         `json:"desired_state,omitempty"`
	ActualState            string               `json:"actual_state,omitempty"`
	Pending                bool                 `json:"pending"`
	Failed                 bool                 `json:"failed"`
	Stale                  bool                 `json:"stale"`
	Reason                 string               `json:"reason,omitempty"`
	LastError              string               `json:"last_error,omitempty"`
	LastObservedAt         *time.Time           `json:"last_observed_at,omitempty"`
	StaleAfterSeconds      int64                `json:"stale_after_seconds"`
	LatestIntent           *ActionIntent        `json:"latest_intent,omitempty"`
}

type ActualSnapshot struct {
	RuntimeID              string    `json:"runtime_id"`
	HostID                 string    `json:"host_id"`
	AgentID                string    `json:"agent_id,omitempty"`
	ActualState            string    `json:"actual_state"`
	ProcessID              int       `json:"process_id,omitempty"`
	StartToken             string    `json:"start_token,omitempty"`
	ConfigVersion          int64     `json:"config_version,omitempty"`
	ConfigPath             string    `json:"config_path,omitempty"`
	ConfigChecksum         string    `json:"config_checksum,omitempty"`
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
	StartedAt              time.Time `json:"started_at,omitempty"`
	UpdatedAt              time.Time `json:"updated_at"`
	LastExitCode           *int      `json:"last_exit_code,omitempty"`
}

type RuntimeEvent struct {
	EventID        string            `json:"event_id,omitempty"`
	RuntimeID      string            `json:"runtime_id"`
	HostID         string            `json:"host_id,omitempty"`
	AgentID        string            `json:"agent_id,omitempty"`
	Type           string            `json:"type"`
	Severity       string            `json:"severity,omitempty"`
	DesiredVersion int64             `json:"desired_version,omitempty"`
	ProcessID      int               `json:"process_id,omitempty"`
	Message        string            `json:"message,omitempty"`
	OccurredAt     time.Time         `json:"occurred_at"`
	Metadata       map[string]string `json:"metadata,omitempty"`
}

type ActionIntent struct {
	ID             string             `json:"intent_id"`
	RequestID      string             `json:"request_id"`
	RuntimeID      string             `json:"runtime_id"`
	HostID         string             `json:"host_id,omitempty"`
	Kind           ActionKind         `json:"kind"`
	Status         ActionIntentStatus `json:"status"`
	DesiredVersion int64              `json:"desired_version"`
	Payload        map[string]any     `json:"payload,omitempty"`
	CreatedAt      time.Time          `json:"created_at"`
	UpdatedAt      time.Time          `json:"updated_at"`
	ProjectedAt    *time.Time         `json:"projected_at,omitempty"`
	ObservedAt     *time.Time         `json:"observed_at,omitempty"`
	SupersededAt   *time.Time         `json:"superseded_at,omitempty"`
	FailedAt       *time.Time         `json:"failed_at,omitempty"`
	FailureReason  string             `json:"failure_reason,omitempty"`
}

type ActionRequest struct {
	RequestID string         `json:"request_id,omitempty"`
	Payload   map[string]any `json:"payload,omitempty"`
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

type ControlPlaneStatus struct {
	Status               string     `json:"status"`
	Repository           string     `json:"repository"`
	HostCount            int        `json:"host_count"`
	RuntimeCount         int        `json:"runtime_count"`
	DesiredCount         int        `json:"desired_count"`
	LatestDesiredVersion int64      `json:"latest_desired_version"`
	PendingIntentCount   int        `json:"pending_intent_count"`
	FailedIntentCount    int        `json:"failed_intent_count"`
	StaleRuntimeCount    int        `json:"stale_runtime_count"`
	UpdatedAt            *time.Time `json:"updated_at,omitempty"`
	Error                string     `json:"error,omitempty"`
}
