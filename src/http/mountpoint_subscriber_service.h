#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

namespace navcaster::http_api
{

class MountpointSubscriberService
{
public:
    explicit MountpointSubscriberService(storage::RedisHashClient &redis);

    ControllerResponse list();

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
