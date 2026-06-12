#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"
#include "system_event_repository.h"

namespace navcaster::http_api
{

class SystemEventService
{
public:
    explicit SystemEventService(storage::RedisHashClient &redis);

    ControllerResponse list(long long limit);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
