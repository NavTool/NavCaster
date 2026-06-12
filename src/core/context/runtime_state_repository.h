#pragma once

#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

enum class RuntimeStateKind
{
    Server,
    Client,
    Stream,
    Node
};

const char *runtime_state_key(RuntimeStateKind kind);

class RuntimeStateRepository
{
public:
    explicit RuntimeStateRepository(RedisHashClient &redis);

    nlohmann::json list(RuntimeStateKind kind);
    nlohmann::json get(RuntimeStateKind kind, const std::string &uid);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
