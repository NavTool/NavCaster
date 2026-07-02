package runtime

import "time"

type DesiredStateValue string

const (
	DesiredStateRunning  DesiredStateValue = "running"
	DesiredStateStopped  DesiredStateValue = "stopped"
	DesiredStateDraining DesiredStateValue = "draining"
	DesiredStateDeleted  DesiredStateValue = "deleted"
)

type ActualStateValue string

const (
	ActualStateMissing  ActualStateValue = "missing"
	ActualStateUnknown  ActualStateValue = "unknown"
	ActualStateStarting ActualStateValue = "starting"
	ActualStateRunning  ActualStateValue = "running"
	ActualStateStopping ActualStateValue = "stopping"
	ActualStateStopped  ActualStateValue = "stopped"
	ActualStateFailed   ActualStateValue = "failed"
)

type RestartPolicy string

const (
	RestartPolicyNever     RestartPolicy = "never"
	RestartPolicyOnFailure RestartPolicy = "on_failure"
	RestartPolicyAlways    RestartPolicy = "always"
)

type RuntimeKind string

const (
	RuntimeKindDummy   RuntimeKind = "dummy"
	RuntimeKindCommand RuntimeKind = "command"
	RuntimeKindCaster  RuntimeKind = "caster"
)

type DesiredState struct {
	RuntimeID      string            `json:"runtime_id"`
	HostID         string            `json:"host_id,omitempty"`
	DesiredState   DesiredStateValue `json:"desired_state"`
	RuntimeKind    RuntimeKind       `json:"runtime_kind,omitempty"`
	ConfigVersion  int               `json:"config_version"`
	ListenHost     string            `json:"listen_host,omitempty"`
	ListenPort     int               `json:"listen_port,omitempty"`
	HealthHost     string            `json:"health_host,omitempty"`
	HealthPort     int               `json:"health_port,omitempty"`
	WorkerCount    int               `json:"worker_count,omitempty"`
	MaxWorkerCount int               `json:"max_worker_count,omitempty"`
	RestartPolicy  RestartPolicy     `json:"restart_policy,omitempty"`
	Draining       bool              `json:"draining,omitempty"`
	Version        int64             `json:"version"`
	Generation     int64             `json:"generation,omitempty"`
	UpdatedBy      string            `json:"updated_by,omitempty"`
	UpdatedAt      time.Time         `json:"updated_at,omitempty"`
	Command        string            `json:"command,omitempty"`
	Args           []string          `json:"args,omitempty"`
	Env            map[string]string `json:"env,omitempty"`
	WorkingDir     string            `json:"working_dir,omitempty"`
	ConfigPath     string            `json:"config_path,omitempty"`
	ConfigChecksum string            `json:"config_checksum,omitempty"`
}

func (d DesiredState) NormalizedRestartPolicy() RestartPolicy {
	if d.RestartPolicy == "" {
		return RestartPolicyOnFailure
	}
	return d.RestartPolicy
}

func (d DesiredState) WantsRunning() bool {
	return d.DesiredState == DesiredStateRunning || d.DesiredState == DesiredStateDraining
}

type DesiredDocument struct {
	Version   int64          `json:"version"`
	Runtimes  []DesiredState `json:"runtimes"`
	UpdatedAt time.Time      `json:"updated_at,omitempty"`
}

type ActualState struct {
	RuntimeID              string           `json:"runtime_id"`
	HostID                 string           `json:"host_id,omitempty"`
	AgentID                string           `json:"agent_id,omitempty"`
	ActualState            ActualStateValue `json:"actual_state"`
	ProcessID              int              `json:"process_id,omitempty"`
	StartToken             string           `json:"start_token,omitempty"`
	ConfigVersion          int              `json:"config_version,omitempty"`
	ConfigPath             string           `json:"config_path,omitempty"`
	ConfigChecksum         string           `json:"config_checksum,omitempty"`
	ListenPort             int              `json:"listen_port,omitempty"`
	WorkerCount            int              `json:"worker_count,omitempty"`
	Connections            int              `json:"connections,omitempty"`
	Mounts                 int              `json:"mounts,omitempty"`
	Sources                int              `json:"sources,omitempty"`
	Clients                int              `json:"clients,omitempty"`
	SendBPS                int64            `json:"send_bps,omitempty"`
	RecvBPS                int64            `json:"recv_bps,omitempty"`
	LoopDelayP95MS         int              `json:"loop_delay_ms_p95,omitempty"`
	RedisConnected         bool             `json:"redis_connected,omitempty"`
	ObservedDesiredVersion int64            `json:"observed_desired_version,omitempty"`
	StartedAt              time.Time        `json:"started_at,omitempty"`
	UpdatedAt              time.Time        `json:"updated_at"`
	LastExitCode           *int             `json:"last_exit_code,omitempty"`
	LastError              string           `json:"last_error,omitempty"`
}

func (a ActualState) IsRunning() bool {
	return a.ActualState == ActualStateRunning || a.ActualState == ActualStateStarting
}

func (a ActualState) HasLiveProcess() bool {
	return a.ProcessID > 0 && a.ActualState != ActualStateStopped && a.ActualState != ActualStateMissing
}

type Event struct {
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
