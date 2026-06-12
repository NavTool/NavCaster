#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <string>
#include <unordered_map>

namespace navcaster::http_api
{

class StatisticsController
{
public:
    StatisticsController(storage::RedisHashClient &redis, long long now_ts);

    ControllerResponse overview(const std::unordered_map<std::string, std::string> &query_params);
    ControllerResponse daily(const std::string &date);
    ControllerResponse mountpoint_ranking(const std::unordered_map<std::string, std::string> &query_params);
    ControllerResponse user_ranking(const std::unordered_map<std::string, std::string> &query_params);

private:
    storage::RedisHashClient &_redis;
    long long _now_ts = 0;
};

long long statistics_parse_time_param(const std::unordered_map<std::string, std::string> &params, const char *name);
bool statistics_parse_date_start(const std::string &date, long long &start_ts);
long long statistics_today_start(long long now_ts);
int statistics_limit_param(const std::unordered_map<std::string, std::string> &params);

} // namespace navcaster::http_api
