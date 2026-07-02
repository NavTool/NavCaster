package projection

import "navcaster-admin/internal/storage/redis"

type Registry struct {
	redis redis.Registry
}

func NewRegistry(redisRegistry redis.Registry) Registry {
	return Registry{redis: redisRegistry}
}

func (r Registry) RedisKeys() []redis.KeyDefinition {
	return r.redis.List()
}

func (r Registry) RuntimeConfigKey(runtimeID string) (string, error) {
	return r.redis.Render("runtime_config", map[string]string{"runtime_id": runtimeID})
}

func (r Registry) HostDesiredStateKey(hostID string) (string, error) {
	return r.redis.Render("host_desired_state", map[string]string{"host_id": hostID})
}

func (r Registry) ConfigPubSubChannel() (string, error) {
	return r.redis.Render("config_pubsub", nil)
}

func (r Registry) RuntimeActualKey(runtimeID string) (string, error) {
	return r.redis.Render("runtime_actual", map[string]string{"runtime_id": runtimeID})
}

func (r Registry) ActionIntentKey(intentID string) (string, error) {
	return r.redis.Render("action_intent", map[string]string{"intent_id": intentID})
}

func (r Registry) AgentHeartbeatKey(agentID string) (string, error) {
	return r.redis.Render("agent_heartbeat", map[string]string{"agent_id": agentID})
}

func (r Registry) AccessAccountAuthKey(username string) (string, error) {
	return r.redis.Render("access_account_auth", map[string]string{"username": username})
}

func (r Registry) AccessAccountPolicyKey(accessAccountID string) (string, error) {
	return r.redis.Render("access_account_policy", map[string]string{"access_account_id": accessAccountID})
}
