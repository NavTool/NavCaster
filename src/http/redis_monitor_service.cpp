#include "redis_monitor_service.h"

#include "controller_helpers.h"
#include "redis_monitor_repository.h"

namespace navcaster::http_api
{

long long redis_monitor_history_limit(const std::string &range)
{
    if (range == "6h")
    {
        return 360;
    }
    if (range == "24h")
    {
        return 1440;
    }
    if (range == "7d")
    {
        return 10080;
    }
    return 60;
}

nlohmann::json redis_monitor_history_items(const nlohmann::json &raw_items)
{
    nlohmann::json items = nlohmann::json::array();
    if (!raw_items.is_array())
    {
        return items;
    }

    for (auto it = raw_items.rbegin(); it != raw_items.rend(); ++it)
    {
        if (!it->is_object() || it->value("t", 0LL) <= 0 || it->value("used_memory", 0ULL) == 0)
        {
            continue;
        }
        items.push_back(*it);
    }
    return items;
}

RedisMonitorService::RedisMonitorService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse RedisMonitorService::history(const std::string &range)
{
    storage::RedisMonitorRepository repo(_redis);
    auto items = redis_monitor_history_items(repo.history(redis_monitor_history_limit(range)));
    return json_response(200, {{"items", items}, {"count", items.size()}});
}

} // namespace navcaster::http_api
