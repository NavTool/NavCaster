#include "runtime_state_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

const char *runtime_state_key(RuntimeStateKind kind)
{
    switch (kind)
    {
    case RuntimeStateKind::Server:
        return redis_keys::MPT_STAT;
    case RuntimeStateKind::Client:
        return redis_keys::USR_STAT;
    case RuntimeStateKind::Stream:
        return redis_keys::STR_STAT;
    case RuntimeStateKind::Node:
        return redis_keys::CASTER_NODE;
    }
    return redis_keys::STR_STAT;
}

RuntimeStateRepository::RuntimeStateRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json RuntimeStateRepository::list(RuntimeStateKind kind)
{
    return _redis.hgetall(runtime_state_key(kind));
}

nlohmann::json RuntimeStateRepository::get(RuntimeStateKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return nullptr;
    }
    return _redis.hget(runtime_state_key(kind), uid.c_str());
}

} // namespace navcaster::storage
