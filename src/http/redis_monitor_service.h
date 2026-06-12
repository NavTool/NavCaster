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

class RedisMonitorService
{
public:
    explicit RedisMonitorService(storage::RedisHashClient &redis);

    ControllerResponse summary();
    ControllerResponse history(const std::string &range);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
