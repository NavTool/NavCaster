#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

long long redis_monitor_history_limit(const std::string &range);
nlohmann::json redis_monitor_history_items(const nlohmann::json &raw_items);
nlohmann::json parse_redis_info(const std::string &info_text);
nlohmann::json redis_monitor_summary_body(const nlohmann::json &info, long long total_keys);
std::string redis_monitor_key_prefix(const std::string &key);
constexpr long long REDIS_MONITOR_HISTORY_KEEP = 10080;

class RedisMonitorService
{
public:
    explicit RedisMonitorService(storage::RedisHashClient &redis);

    ControllerResponse summary();
    ControllerResponse keys();
    ControllerResponse history(const std::string &range);
    bool sample_history(long long now_ts);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
