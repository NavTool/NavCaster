#pragma once

#include "redis_hash_client.h"

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

class SystemEventRepository
{
public:
    explicit SystemEventRepository(RedisHashClient &redis);

    nlohmann::json list_node_events();

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
