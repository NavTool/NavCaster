#include "system_event_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

SystemEventRepository::SystemEventRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json SystemEventRepository::list_node_events()
{
    nlohmann::json events = nlohmann::json::array();
    for (const auto &key : _redis.scan_all_keys(500))
    {
        const std::string prefix = redis_keys::LOG_NODE_PREFIX;
        if (key.rfind(prefix, 0) != 0)
        {
            continue;
        }

        auto node_events = _redis.hgetall(key.c_str());
        if (!node_events.is_object())
        {
            continue;
        }
        for (const auto &[field, event] : node_events.items())
        {
            (void)field;
            if (event.is_object())
            {
                events.push_back(event);
            }
        }
    }
    return events;
}

} // namespace navcaster::storage
