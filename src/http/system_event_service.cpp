#include "system_event_service.h"

#include "controller_helpers.h"

#include <algorithm>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

long long normalize_limit(long long limit)
{
    if (limit <= 0 || limit > 500)
    {
        return 100;
    }
    return limit;
}
} // namespace

SystemEventService::SystemEventService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse SystemEventService::list(long long limit)
{
    const long long normalized_limit = normalize_limit(limit);
    storage::SystemEventRepository repo(_redis);
    json items = repo.list_node_events();
    std::sort(items.begin(), items.end(), [](const json &left, const json &right) {
        return left.value("timestamp", 0ULL) > right.value("timestamp", 0ULL);
    });
    if (static_cast<long long>(items.size()) > normalized_limit)
    {
        items.erase(items.begin() + normalized_limit, items.end());
    }
    return json_response(200, json{{"items", items}, {"count", items.size()}});
}

} // namespace navcaster::http_api
