#pragma once

#include "redis_hash_client.h"

#include <string>

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
    nlohmann::json detail(ConnectionHistoryKind kind, const std::string &name);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
