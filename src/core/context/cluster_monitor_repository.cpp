#include "cluster_monitor_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

ClusterMonitorRepository::ClusterMonitorRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

std::string ClusterMonitorRepository::master_node()
{
    auto value = _redis.get(redis_keys::CASTER_MASTER);
    return value.is_string() ? value.get<std::string>() : std::string();
}

nlohmann::json ClusterMonitorRepository::nodes()
{
    return _redis.hgetall(redis_keys::CASTER_NODE);
}

nlohmann::json ClusterMonitorRepository::pull_states()
{
    return _redis.hgetall(redis_keys::PULL_STAT);
}

nlohmann::json ClusterMonitorRepository::push_states()
{
    return _redis.hgetall(redis_keys::PUSH_STAT);
}

} // namespace navcaster::storage
