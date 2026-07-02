#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"
#include "runtime_state_repository.h"

#include <string>

namespace navcaster::http_api
{

class RuntimeStateController
{
public:
    explicit RuntimeStateController(storage::RedisHashClient &redis);

    ControllerResponse list(storage::RuntimeStateKind kind);
    ControllerResponse get(storage::RuntimeStateKind kind, const std::string &uid);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
