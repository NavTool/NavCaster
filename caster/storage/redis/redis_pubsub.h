#pragma once

#include "storage/redis/redis_async_context.h"

namespace navcaster::caster {

struct WorkerRedisBoundary {
    RedisAsyncContext command;
    RedisAsyncContext pubsub;

    WorkerRedisBoundary() = default;
    WorkerRedisBoundary(const std::string &host, int port);
};

} // namespace navcaster::caster
