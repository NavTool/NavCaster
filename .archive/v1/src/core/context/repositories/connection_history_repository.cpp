#include "connection_history_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

const char *connection_history_prefix(ConnectionHistoryKind kind)
{
    switch (kind)
    {
    case ConnectionHistoryKind::Server:
        return redis_keys::LOG_MPT_PREFIX;
    case ConnectionHistoryKind::Client:
        return redis_keys::LOG_USR_PREFIX;
    }
    return redis_keys::LOG_MPT_PREFIX;
}

ConnectionHistoryRepository::ConnectionHistoryRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json ConnectionHistoryRepository::list(ConnectionHistoryKind kind)
{
    return _redis.scan_hgetall_prefix(connection_history_prefix(kind));
}

nlohmann::json ConnectionHistoryRepository::detail(ConnectionHistoryKind kind, const std::string &name)
{
    if (name.empty())
    {
        return nlohmann::json::object();
    }

    const std::string key = kind == ConnectionHistoryKind::Server
        ? redis_keys::log_mpt(name)
        : redis_keys::log_usr(name);
    return _redis.hgetall(key.c_str());
}

} // namespace navcaster::storage
