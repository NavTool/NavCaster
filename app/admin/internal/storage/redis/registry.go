package redis

import (
	"fmt"
	"sort"
	"strings"
)

type Persistence string

const (
	PersistenceProjection Persistence = "projection"
	PersistenceRuntimeTTL Persistence = "runtime_ttl"
	PersistencePubSub     Persistence = "pubsub"
)

type KeyDefinition struct {
	Name        string      `json:"name"`
	Pattern     string      `json:"pattern"`
	Scope       string      `json:"scope"`
	Owner       string      `json:"owner"`
	Writer      string      `json:"writer"`
	Reader      string      `json:"reader"`
	ValueType   string      `json:"value_type"`
	TTLSeconds  int         `json:"ttl_seconds,omitempty"`
	Persistence Persistence `json:"persistence"`
	Description string      `json:"description"`
}

type Registry struct {
	keys map[string]KeyDefinition
}

func NewRegistry(keys []KeyDefinition) Registry {
	registry := Registry{keys: map[string]KeyDefinition{}}
	for _, key := range keys {
		registry.keys[key.Name] = key
	}
	return registry
}

func DefaultRegistry() Registry {
	return NewRegistry([]KeyDefinition{
		{
			Name:        "access_account_auth",
			Pattern:     "auth:access-account:{username}",
			Scope:       "auth_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-caster auth cache loader",
			ValueType:   "string json: AccessAccountAuthIndex",
			Persistence: PersistenceProjection,
			Description: "Caster authentication fast index by username generated from PostgreSQL access_accounts.",
		},
		{
			Name:        "access_account_policy",
			Pattern:     "auth:policy:{access_account_id}",
			Scope:       "auth_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-caster auth cache loader",
			ValueType:   "string json: access policy projection",
			Persistence: PersistenceProjection,
			Description: "Per access-account policy projection; Caster reads Redis/local cache, never PostgreSQL.",
		},
		{
			Name:        "auth_version",
			Pattern:     "auth:version",
			Scope:       "auth_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-caster auth cache loader",
			ValueType:   "integer",
			Persistence: PersistenceProjection,
			Description: "Latest auth projection version.",
		},
		{
			Name:        "runtime_config",
			Pattern:     "config:runtime:{runtime_id}",
			Scope:       "config_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-agent and navcaster-caster",
			ValueType:   "string json: runtime config projection",
			Persistence: PersistenceProjection,
			Description: "Published runtime configuration projection derived from config_versions.",
		},
		{
			Name:        "host_desired_state",
			Pattern:     "control:desired-state:{host_id}",
			Scope:       "config_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-agent",
			ValueType:   "string json: DesiredStateResponse",
			Persistence: PersistenceProjection,
			Description: "Host-level desired-state projection matching the agent polling response shape.",
		},
		{
			Name:        "config_version",
			Pattern:     "config:version",
			Scope:       "config_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-agent and navcaster-caster",
			ValueType:   "integer",
			Persistence: PersistenceProjection,
			Description: "Latest published global config projection version.",
		},
		{
			Name:        "config_pubsub",
			Pattern:     "control:config",
			Scope:       "pubsub",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-agent and navcaster-caster",
			ValueType:   "json config change event",
			Persistence: PersistencePubSub,
			Description: "Config projection invalidation bus.",
		},
		{
			Name:        "action_intent",
			Pattern:     "control:intent:{intent_id}",
			Scope:       "control_projection",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-admin, navcaster-agent, and QA smoke",
			ValueType:   "string json: action intent projection",
			Persistence: PersistenceProjection,
			Description: "Auditable control action intent projection keyed by intent_id.",
		},
		{
			Name:        "runtime_actual",
			Pattern:     "runtime:actual:{runtime_id}",
			Scope:       "runtime_state",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin runtime-metrics ingest",
			Reader:      "navcaster-admin",
			ValueType:   "string json: runtime actual snapshot",
			TTLSeconds:  60,
			Persistence: PersistenceRuntimeTTL,
			Description: "Short-lived actual runtime state; long-term snapshots belong in PostgreSQL.",
		},
		{
			Name:        "worker_stat",
			Pattern:     "runtime:worker-stat:{runtime_id}",
			Scope:       "runtime_state",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-admin",
			ValueType:   "redis hash: worker_id -> WorkerStat JSON",
			TTLSeconds:  60,
			Persistence: PersistenceRuntimeTTL,
			Description: "Worker metrics generated by Caster runtime.",
		},
		{
			Name:        "runtime_mount_owner",
			Pattern:     "runtime:mount-owner:{runtime_id}",
			Scope:       "runtime_state",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-admin",
			ValueType:   "redis hash: mount -> worker_id",
			TTLSeconds:  60,
			Persistence: PersistenceRuntimeTTL,
			Description: "Runtime mount owner snapshot for control-plane display.",
		},
		{
			Name:        "agent_heartbeat",
			Pattern:     "agent:heartbeat:{agent_id}",
			Scope:       "runtime_state",
			Owner:       "navcaster-agent",
			Writer:      "navcaster-agent",
			Reader:      "navcaster-admin",
			ValueType:   "string json: agent heartbeat",
			TTLSeconds:  45,
			Persistence: PersistenceRuntimeTTL,
			Description: "Short-lived agent heartbeat projection for operational visibility.",
		},
		{
			Name:        "access_account_session",
			Pattern:     "session:access-account:{access_account_id}",
			Scope:       "online_session",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-admin",
			ValueType:   "redis hash: connect_key -> OnlineSession JSON",
			TTLSeconds:  60,
			Persistence: PersistenceRuntimeTTL,
			Description: "Online sessions grouped by AccessAccount.",
		},
		{
			Name:        "account_session",
			Pattern:     "session:account:{account_id}",
			Scope:       "online_session",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-admin",
			ValueType:   "redis hash: connect_key -> OnlineSession JSON",
			TTLSeconds:  60,
			Persistence: PersistenceRuntimeTTL,
			Description: "Online sessions grouped by owning Web account.",
		},
		{
			Name:        "mount_session",
			Pattern:     "session:mount:{mount}",
			Scope:       "online_session",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-admin",
			ValueType:   "redis hash: connect_key -> session summary JSON",
			TTLSeconds:  60,
			Persistence: PersistenceRuntimeTTL,
			Description: "Online sessions grouped by mount.",
		},
		{
			Name:        "sourcetable_runtime",
			Pattern:     "sourcetable:runtime:{runtime_id}",
			Scope:       "sourcetable",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-caster",
			ValueType:   "string json: runtime sourcetable snapshot",
			TTLSeconds:  30,
			Persistence: PersistenceRuntimeTTL,
			Description: "Short-lived sourcetable snapshot for a single runtime.",
		},
		{
			Name:        "sourcetable_index",
			Pattern:     "sourcetable:index",
			Scope:       "sourcetable",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-caster",
			ValueType:   "set or string json: runtime sourcetable snapshot index",
			TTLSeconds:  30,
			Persistence: PersistenceRuntimeTTL,
			Description: "Optional runtime sourcetable snapshot index for controlled refresh.",
		},
		{
			Name:        "sourcetable_changed",
			Pattern:     "sourcetable:changed",
			Scope:       "sourcetable",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-caster",
			ValueType:   "json sourcetable change event",
			Persistence: PersistencePubSub,
			Description: "Optional sourcetable snapshot change notification bus.",
		},
		{
			Name:        "mount_stream",
			Pattern:     "stream:mount:{mount}",
			Scope:       "pubsub",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-caster",
			ValueType:   "binary rtcm payload",
			Persistence: PersistencePubSub,
			Description: "Mount data stream channel.",
		},
		{
			Name:        "runtime_stream",
			Pattern:     "stream:runtime:{runtime_id}",
			Scope:       "pubsub",
			Owner:       "navcaster-caster",
			Writer:      "navcaster-caster",
			Reader:      "navcaster-agent",
			ValueType:   "json runtime event",
			Persistence: PersistencePubSub,
			Description: "Runtime control or observation event stream.",
		},
		{
			Name:        "control_kick",
			Pattern:     "control:kick",
			Scope:       "pubsub",
			Owner:       "navcaster-admin",
			Writer:      "navcaster-admin projection worker",
			Reader:      "navcaster-caster",
			ValueType:   "json kick or policy change event",
			Persistence: PersistencePubSub,
			Description: "Kick and access policy change notification bus.",
		},
	})
}

func (r Registry) List() []KeyDefinition {
	keys := make([]KeyDefinition, 0, len(r.keys))
	for _, key := range r.keys {
		keys = append(keys, key)
	}
	sort.Slice(keys, func(i, j int) bool {
		return keys[i].Name < keys[j].Name
	})
	return keys
}

func (r Registry) Get(name string) (KeyDefinition, bool) {
	key, ok := r.keys[name]
	return key, ok
}

func (r Registry) Render(name string, values map[string]string) (string, error) {
	key, ok := r.Get(name)
	if !ok {
		return "", fmt.Errorf("redis key definition %q not found", name)
	}
	rendered := key.Pattern
	for _, token := range placeholders(key.Pattern) {
		value := values[token]
		if value == "" {
			return "", fmt.Errorf("redis key %q requires value %q", name, token)
		}
		rendered = strings.ReplaceAll(rendered, "{"+token+"}", value)
	}
	return rendered, nil
}

func placeholders(pattern string) []string {
	var out []string
	for {
		start := strings.IndexByte(pattern, '{')
		if start < 0 {
			return out
		}
		end := strings.IndexByte(pattern[start:], '}')
		if end < 0 {
			return out
		}
		token := pattern[start+1 : start+end]
		out = append(out, token)
		pattern = pattern[start+end+1:]
	}
}
