#pragma once

#include "connection_history_repository.h"
#include "controller_response.h"
#include "redis_hash_client.h"

#include <string>

namespace navcaster::http_api
{

class ConnectionHistoryService
{
public:
    explicit ConnectionHistoryService(storage::RedisHashClient &redis);

    ControllerResponse list(storage::ConnectionHistoryKind kind);
    ControllerResponse detail(storage::ConnectionHistoryKind kind, const std::string &name, long long now_ts);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
