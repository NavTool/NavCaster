#pragma once

#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

class AuditLogRepository
{
public:
    explicit AuditLogRepository(RedisHashClient &redis);

    long long next_id();
    void append(const nlohmann::json &entry, int keep);
    nlohmann::json read(long long start, long long stop);
    long long total();

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
