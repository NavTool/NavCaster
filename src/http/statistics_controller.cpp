#include "statistics_controller.h"

#include "controller_helpers.h"
#include "redis_keys.h"
#include "statistics_service.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

constexpr int DAILY_CACHE_SECONDS = 604800;

bool parse_yyyy_mm_dd(const std::string &value, std::tm &tm_value)
{
    std::tm parsed{};
    std::istringstream stream(value);
    stream >> std::get_time(&parsed, "%Y-%m-%d");
    if (stream.fail())
    {
        return false;
    }
    tm_value = parsed;
    return true;
}

bool parse_date_range(const std::string &date, long long &start_ts, long long &end_ts)
{
    std::tm tm_value{};
    if (!parse_yyyy_mm_dd(date, tm_value))
    {
        return false;
    }
    start_ts = static_cast<long long>(std::mktime(&tm_value));
    tm_value.tm_mday += 1;
    end_ts = static_cast<long long>(std::mktime(&tm_value));
    return true;
}

std::tm local_time_snapshot(std::time_t value)
{
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

void apply_date_override(const std::unordered_map<std::string, std::string> &params,
                         long long &start_ts,
                         long long &end_ts,
                         bool &has_start,
                         bool &has_end)
{
    auto date_it = params.find("date");
    if (date_it == params.end() || date_it->second.empty())
    {
        return;
    }

    long long day_start = 0;
    long long day_end = 0;
    if (!parse_date_range(date_it->second, day_start, day_end))
    {
        return;
    }
    start_ts = day_start;
    end_ts = day_end;
    has_start = true;
    has_end = true;
}

bool parse_time_param(const std::unordered_map<std::string, std::string> &params, const char *name, long long &value)
{
    auto it = params.find(name);
    if (it == params.end() || it->second.empty())
    {
        value = 0;
        return false;
    }

    try
    {
        value = std::stoll(it->second);
        return true;
    }
    catch (...)
    {
    }

    long long start_ts = 0;
    if (statistics_parse_date_start(it->second, start_ts))
    {
        value = start_ts;
        return true;
    }

    value = 0;
    return false;
}

void apply_default_time_range(long long now_ts,
                              long long &start_ts,
                              long long &end_ts,
                              bool has_start,
                              bool has_end)
{
    if (!has_start)
    {
        start_ts = statistics_today_start(now_ts);
    }
    if (!has_end)
    {
        end_ts = now_ts + 1;
    }
}
} // namespace

StatisticsController::StatisticsController(storage::RedisHashClient &redis, long long now_ts)
    : _redis(redis), _now_ts(now_ts)
{
}

long long statistics_parse_time_param(const std::unordered_map<std::string, std::string> &params, const char *name)
{
    long long value = 0;
    parse_time_param(params, name, value);
    return value;
}

bool statistics_parse_date_start(const std::string &date, long long &start_ts)
{
    std::tm tm_value{};
    if (!parse_yyyy_mm_dd(date, tm_value))
    {
        return false;
    }
    start_ts = static_cast<long long>(std::mktime(&tm_value));
    return true;
}

long long statistics_today_start(long long now_ts)
{
    const std::time_t now_time = static_cast<std::time_t>(now_ts);
    std::tm today = local_time_snapshot(now_time);
    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    return static_cast<long long>(std::mktime(&today));
}

int statistics_limit_param(const std::unordered_map<std::string, std::string> &params)
{
    int limit = 20;
    auto it = params.find("limit");
    if (it != params.end())
    {
        try
        {
            limit = std::stoi(it->second);
        }
        catch (...)
        {
        }
    }
    if (limit <= 0)
    {
        limit = 20;
    }
    if (limit > 100)
    {
        limit = 100;
    }
    return limit;
}

ControllerResponse StatisticsController::overview(const std::unordered_map<std::string, std::string> &query_params)
{
    long long start_ts = 0;
    long long end_ts = 0;
    bool has_start = parse_time_param(query_params, "start", start_ts);
    bool has_end = parse_time_param(query_params, "end", end_ts);
    apply_date_override(query_params, start_ts, end_ts, has_start, has_end);
    apply_default_time_range(_now_ts, start_ts, end_ts, has_start, has_end);

    StatisticsService service;
    json result = service.overview(
        _redis.scan_hgetall_prefix(redis_keys::LOG_MPT_PREFIX),
        _redis.scan_hgetall_prefix(redis_keys::LOG_USR_PREFIX),
        start_ts,
        end_ts,
        _now_ts);
    return json_response(200, result);
}

ControllerResponse StatisticsController::daily(const std::string &date)
{
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
    {
        return error_response(400, "Invalid date format, use YYYY-MM-DD");
    }

    long long start_ts = 0;
    long long end_ts = 0;
    if (!parse_date_range(date, start_ts, end_ts))
    {
        return error_response(400, "Invalid date");
    }
    const bool is_past = end_ts <= _now_ts;
    const std::string cache_key = redis_keys::stat_daily(date);
    if (is_past)
    {
        json cached = _redis.get(cache_key.c_str());
        if (!cached.is_null())
        {
            ControllerResponse response;
            response.status_code = 200;
            response.body = cached.is_string() ? cached.get<std::string>() : cached.dump();
            return response;
        }
    }

    StatisticsService service;
    json result = service.daily(
        date,
        _redis.scan_hgetall_prefix(redis_keys::LOG_MPT_PREFIX),
        _redis.scan_hgetall_prefix(redis_keys::LOG_USR_PREFIX),
        start_ts,
        end_ts,
        _now_ts);

    std::string body = result.dump();
    if (is_past)
    {
        _redis.setex(cache_key.c_str(), DAILY_CACHE_SECONDS, body);
    }

    ControllerResponse response;
    response.status_code = 200;
    response.body = std::move(body);
    return response;
}

ControllerResponse StatisticsController::mountpoint_ranking(const std::unordered_map<std::string, std::string> &query_params)
{
    long long start_ts = 0;
    long long end_ts = 0;
    const bool has_start = parse_time_param(query_params, "start", start_ts);
    const bool has_end = parse_time_param(query_params, "end", end_ts);
    apply_default_time_range(_now_ts, start_ts, end_ts, has_start, has_end);

    StatisticsService service;
    return json_response(200, service.mountpoint_ranking(
        _redis.scan_hgetall_prefix(redis_keys::LOG_MPT_PREFIX),
        start_ts,
        end_ts,
        _now_ts,
        statistics_limit_param(query_params)));
}

ControllerResponse StatisticsController::user_ranking(const std::unordered_map<std::string, std::string> &query_params)
{
    long long start_ts = 0;
    long long end_ts = 0;
    const bool has_start = parse_time_param(query_params, "start", start_ts);
    const bool has_end = parse_time_param(query_params, "end", end_ts);
    apply_default_time_range(_now_ts, start_ts, end_ts, has_start, has_end);

    StatisticsService service;
    return json_response(200, service.user_ranking(
        _redis.scan_hgetall_prefix(redis_keys::LOG_USR_PREFIX),
        start_ts,
        end_ts,
        _now_ts,
        statistics_limit_param(query_params)));
}

} // namespace navcaster::http_api
