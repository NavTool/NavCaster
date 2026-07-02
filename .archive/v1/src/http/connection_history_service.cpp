#include "connection_history_service.h"

#include "controller_helpers.h"

#include <algorithm>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

const char *missing_name_error(storage::ConnectionHistoryKind kind)
{
    return kind == storage::ConnectionHistoryKind::Server ? "Missing mountpoint name" : "Missing user name";
}

json history_items_with_duration(const json &logs, long long now_ts)
{
    json result = json::array();
    for (const auto &[field, entry] : logs.items())
    {
        (void)field;
        if (!entry.is_object())
        {
            continue;
        }

        json item = entry;
        const long long connect_time = entry.value("connect_time", 0LL);
        const long long disconnect_time = entry.value("disconnect_time", 0LL);
        item["duration"] = (disconnect_time > 0 ? disconnect_time : now_ts) - connect_time;
        item["online"] = disconnect_time == 0;
        result.push_back(item);
    }

    std::sort(result.begin(), result.end(), [](const json &left, const json &right) {
        return left.value("connect_time", 0LL) > right.value("connect_time", 0LL);
    });
    return result;
}
} // namespace

ConnectionHistoryService::ConnectionHistoryService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse ConnectionHistoryService::list(storage::ConnectionHistoryKind kind)
{
    storage::ConnectionHistoryRepository repo(_redis);
    return json_response(200, repo.list(kind));
}

ControllerResponse ConnectionHistoryService::detail(storage::ConnectionHistoryKind kind, const std::string &name, long long now_ts)
{
    if (name.empty())
    {
        return error_response(400, missing_name_error(kind));
    }

    storage::ConnectionHistoryRepository repo(_redis);
    return json_response(200, history_items_with_duration(repo.detail(kind, name), now_ts));
}

} // namespace navcaster::http_api
