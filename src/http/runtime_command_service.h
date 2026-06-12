#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"
#include "runtime_state_repository.h"

#include <string>

namespace navcaster::http_api
{

class RuntimeCommandService
{
public:
    explicit RuntimeCommandService(storage::RedisHashClient &redis);

    ControllerResponse kick(storage::RuntimeStateKind kind, const std::string &uid);

private:
    storage::RedisHashClient &_redis;
};

std::string make_kick_broadcast_message(storage::RuntimeStateKind kind, const std::string &uid, const std::string &reason);

} // namespace navcaster::http_api
