#include "node_history_service.h"

#include "controller_helpers.h"

namespace navcaster::http_api
{

NodeHistoryService::NodeHistoryService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse NodeHistoryService::list(const std::string &node_id, const std::string &range, long long limit)
{
    if (node_id.empty())
    {
        return error_response(400, "Missing node ID");
    }

    const auto parsed_range = storage::parse_node_history_range(range);
    storage::NodeHistoryRepository repo(_redis);
    return json_response(200, repo.list(node_id, parsed_range, limit));
}

} // namespace navcaster::http_api
