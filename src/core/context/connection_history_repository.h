#pragma once

#include "redis_hash_client.h"

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

enum class ConnectionHistoryKind
{
    Server,
    Client
};

const char *connection_history_prefix(ConnectionHistoryKind kind);

class ConnectionHistoryRepository
{
public:
    explicit ConnectionHistoryRepository(RedisHashClient &redis);

    nlohmann::json list(ConnectionHistoryKind kind);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
