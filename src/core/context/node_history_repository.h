#pragma once

#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

enum class NodeHistoryRange
{
    Raw,
    OneMinute,
    FiveMinutes
};

NodeHistoryRange parse_node_history_range(const std::string &range);
std::string node_history_key(const std::string &node_id, NodeHistoryRange range);
long long node_history_max_limit(NodeHistoryRange range);
long long normalize_node_history_limit(long long limit, NodeHistoryRange range);

class NodeHistoryRepository
{
public:
    explicit NodeHistoryRepository(RedisHashClient &redis);

    nlohmann::json list(const std::string &node_id, NodeHistoryRange range, long long limit);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
