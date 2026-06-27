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

func (r Registry) AccessAccountAuthKey(username string) (string, error) {
	return r.redis.Render("access_account_auth", map[string]string{"username": username})
}

func (r Registry) AccessAccountPolicyKey(accessAccountID string) (string, error) {
	return r.redis.Render("access_account_policy", map[string]string{"access_account_id": accessAccountID})
}
