#pragma once

#include "redis_hash_client.h"

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

class MountpointSubscriberRepository
{
public:
    explicit MountpointSubscriberRepository(RedisHashClient &redis);

    nlohmann::json online_mountpoints();
    long long subscriber_count(const std::string &mountpoint);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
