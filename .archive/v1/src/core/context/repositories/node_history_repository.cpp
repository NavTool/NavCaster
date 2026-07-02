#include "node_history_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

NodeHistoryRange parse_node_history_range(const std::string &range)
{
    if (range == "1m")
    {
        return NodeHistoryRange::OneMinute;
    }
    if (range == "5m")
    {
        return NodeHistoryRange::FiveMinutes;
    }
    return NodeHistoryRange::Raw;
}

std::string node_history_key(const std::string &node_id, NodeHistoryRange range)
{
    switch (range)
    {
    case NodeHistoryRange::OneMinute:
        return redis_keys::node_history_1m(node_id);
    case NodeHistoryRange::FiveMinutes:
        return redis_keys::node_history_5m(node_id);
    case NodeHistoryRange::Raw:
        return redis_keys::node_history(node_id);
    }
    return redis_keys::node_history(node_id);
}

long long node_history_max_limit(NodeHistoryRange range)
{
    switch (range)
    {
    case NodeHistoryRange::OneMinute:
        return 43200;
    case NodeHistoryRange::FiveMinutes:
        return 8640;
    case NodeHistoryRange::Raw:
        return 120960;
    }
    return 120960;
}

long long normalize_node_history_limit(long long limit, NodeHistoryRange range)
{
    if (limit <= 0)
    {
        limit = 17280;
    }
    const long long max_limit = node_history_max_limit(range);
    return limit > max_limit ? max_limit : limit;
}

NodeHistoryRepository::NodeHistoryRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json NodeHistoryRepository::list(const std::string &node_id, NodeHistoryRange range, long long limit)
{
    if (node_id.empty())
    {
        return nlohmann::json::array();
    }
    const std::string key = node_history_key(node_id, range);
    const long long normalized_limit = normalize_node_history_limit(limit, range);
    return _redis.lrange(key.c_str(), 0, normalized_limit - 1);
}

} // namespace navcaster::storage
