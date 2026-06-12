#include "redis_monitor_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

RedisMonitorRepository::RedisMonitorRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json RedisMonitorRepository::history(long long limit)
{
    if (limit <= 0)
    {
        return nlohmann::json::array();
    }
    return _redis.lrange(redis_keys::MONITOR_REDIS_HISTORY, 0, limit - 1);
}

} // namespace navcaster::storage
