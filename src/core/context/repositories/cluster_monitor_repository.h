#pragma once

#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

class ClusterMonitorRepository
{
public:
    explicit ClusterMonitorRepository(RedisHashClient &redis);

    std::string master_node();
    nlohmann::json nodes();
    nlohmann::json pull_states();
    nlohmann::json push_states();

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
