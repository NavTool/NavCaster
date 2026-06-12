#pragma once

#include "redis_hash_client.h"

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

class RedisMonitorRepository
{
public:
    explicit RedisMonitorRepository(RedisHashClient &redis);

    nlohmann::json history(long long limit);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
