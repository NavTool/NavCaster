#include "statistics_service.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

constexpr int TYPE_SERVER = 1;
constexpr int TYPE_CLIENT = 2;
constexpr int TYPE_NEAREST = 3;
constexpr int TYPE_ALIAS = 4;
constexpr int TYPE_PULL = 5;
constexpr int TYPE_PUSH = 6;

struct AggregateState
{
    long long mpt_connections = 0;
    long long usr_connections = 0;
    long long pull_connections = 0;
    long long push_connections = 0;
    long long total_duration_mpt = 0;
    long long total_duration_usr = 0;
    int peak_concurrent_mpt = 0;
    int peak_concurrent_usr = 0;
    int peak_concurrent_pull = 0;
    int peak_concurrent_push = 0;
    std::set<std::string> unique_mounts;
    std::set<std::string> unique_users;
    std::vector<int> mpt_buckets;
    std::vector<int> usr_buckets;
    std::vector<int> pull_buckets;
    std::vector<int> push_buckets;
};

int normalized_limit(int limit)
{
    if (limit <= 0)
        return 20;
    if (limit > 100)
        return 100;
    return limit;
}

int bucket_count(long long start_ts, long long end_ts, long long bucket_seconds, int max_buckets)
{
    const long long range_seconds = std::max(1LL, end_ts - start_ts);
    int count = static_cast<int>((range_seconds + bucket_seconds - 1) / bucket_seconds);
    if (count <= 0)
        count = 1;
    if (count > max_buckets)
        count = max_buckets;
    return count;
}

long long disconnect_or_now(const json &entry, long long now_ts)
{
    long long disconnect_time = entry.value("disconnect_time", 0LL);
    return disconnect_time == 0 ? now_ts : disconnect_time;
}

void mark_bucket(std::vector<int> &buckets,
                 long long connect_time,
                 long long disconnect_time,
                 long long start_ts,
                 long long end_ts,
                 long long bucket_seconds)
{
    const long long bucket_start_ts = std::max(connect_time, start_ts);
    const long long bucket_end_ts = std::min(disconnect_time, end_ts);
    int begin = static_cast<int>((bucket_start_ts - start_ts) / bucket_seconds);
    int end = static_cast<int>((bucket_end_ts - start_ts) / bucket_seconds);
    if (begin < 0)
        begin = 0;
    if (end >= static_cast<int>(buckets.size()))
        end = static_cast<int>(buckets.size()) - 1;
    if (end < begin)
        return;
    for (int i = begin; i <= end; ++i)
    {
        buckets[i]++;
    }
}

void process_aggregate_logs(AggregateState &state,
                            const json &logs,
                            bool is_mpt,
                            long long start_ts,
                            long long end_ts,
                            long long now_ts,
                            long long bucket_seconds)
{
    for (const auto &[field, entry] : logs.items())
    {
        (void)field;
        if (!entry.is_object())
            continue;

        const int type = entry.value("type", 0);
        const bool is_pull = is_mpt && type == TYPE_PULL;
        const bool is_push = !is_mpt && type == TYPE_PUSH;
        const long long connect_time = entry.value("connect_time", 0LL);
        const long long disconnect_time = disconnect_or_now(entry, now_ts);
        if (disconnect_time < start_ts || connect_time >= end_ts)
            continue;

        const long long overlap_start = std::max(connect_time, start_ts);
        const long long overlap_end = std::min(disconnect_time, end_ts);

        if (is_pull)
        {
            state.pull_connections++;
        }
        else if (is_push)
        {
            state.push_connections++;
        }
        else if (is_mpt)
        {
            state.mpt_connections++;
            const std::string name = entry.value("name", std::string{});
            if (!name.empty())
                state.unique_mounts.insert(name);
            state.total_duration_mpt += (overlap_end - overlap_start);
        }
        else
        {
            state.usr_connections++;
            const std::string name = entry.value("name", std::string{});
            if (!name.empty())
                state.unique_users.insert(name);
            state.total_duration_usr += (overlap_end - overlap_start);
        }

        auto &buckets = is_pull  ? state.pull_buckets
                      : is_push  ? state.push_buckets
                      : is_mpt   ? state.mpt_buckets
                                 : state.usr_buckets;
        mark_bucket(buckets, connect_time, disconnect_time, start_ts, end_ts, bucket_seconds);
    }
}

json build_aggregate_result(const json &mpt_logs,
                            const json &usr_logs,
                            long long start_ts,
                            long long end_ts,
                            long long now_ts,
                            long long bucket_seconds,
                            int buckets,
                            const std::string *date,
                            bool include_bucket_seconds)
{
    AggregateState state;
    state.mpt_buckets.assign(buckets, 0);
    state.usr_buckets.assign(buckets, 0);
    state.pull_buckets.assign(buckets, 0);
    state.push_buckets.assign(buckets, 0);

    process_aggregate_logs(state, mpt_logs, true, start_ts, end_ts, now_ts, bucket_seconds);
    process_aggregate_logs(state, usr_logs, false, start_ts, end_ts, now_ts, bucket_seconds);

    for (int i = 0; i < buckets; ++i)
    {
        state.peak_concurrent_mpt = std::max(state.peak_concurrent_mpt, state.mpt_buckets[i]);
        state.peak_concurrent_usr = std::max(state.peak_concurrent_usr, state.usr_buckets[i]);
        state.peak_concurrent_pull = std::max(state.peak_concurrent_pull, state.pull_buckets[i]);
        state.peak_concurrent_push = std::max(state.peak_concurrent_push, state.push_buckets[i]);
    }

    json trend = json::array();
    for (int i = 0; i < buckets; ++i)
    {
        trend.push_back(json{{"ts", start_ts + i * bucket_seconds},
                             {"mpt", state.mpt_buckets[i]},
                             {"usr", state.usr_buckets[i]},
                             {"pull", state.pull_buckets[i]},
                             {"push", state.push_buckets[i]}});
    }

    json result;
    if (date)
        result["date"] = *date;
    result["start"] = start_ts;
    result["end"] = end_ts;
    result["mpt_connections"] = state.mpt_connections;
    result["usr_connections"] = state.usr_connections;
    result["pull_connections"] = state.pull_connections;
    result["push_connections"] = state.push_connections;
    result["peak_concurrent_mpt"] = state.peak_concurrent_mpt;
    result["peak_concurrent_usr"] = state.peak_concurrent_usr;
    result["peak_concurrent_pull"] = state.peak_concurrent_pull;
    result["peak_concurrent_push"] = state.peak_concurrent_push;
    result["avg_duration_mpt"] = state.mpt_connections > 0 ? state.total_duration_mpt / state.mpt_connections : 0;
    result["avg_duration_usr"] = state.usr_connections > 0 ? state.total_duration_usr / state.usr_connections : 0;
    result["unique_mountpoints"] = static_cast<int>(state.unique_mounts.size());
    result["unique_users"] = static_cast<int>(state.unique_users.size());
    result["hourly_trend"] = std::move(trend);
    if (include_bucket_seconds)
        result["bucket_seconds"] = bucket_seconds;
    return result;
}

long long overlap_duration(const json &entry, long long start_ts, long long end_ts, long long now_ts)
{
    const long long connect_time = entry.value("connect_time", 0LL);
    const long long disconnect_time = disconnect_or_now(entry, now_ts);
    return std::min(disconnect_time, end_ts) - std::max(connect_time, start_ts);
}

bool overlaps_range(const json &entry, long long start_ts, long long end_ts, long long now_ts)
{
    const long long connect_time = entry.value("connect_time", 0LL);
    const long long disconnect_time = disconnect_or_now(entry, now_ts);
    return disconnect_time >= start_ts && connect_time < end_ts;
}
} // namespace

json StatisticsService::overview(const json &mpt_logs,
                                 const json &usr_logs,
                                 long long start_ts,
                                 long long end_ts,
                                 long long now_ts) const
{
    const long long range_seconds = std::max(1LL, end_ts - start_ts);
    long long bucket_seconds = 7LL * 86400LL;
    if (range_seconds <= 48LL * 3600LL)
        bucket_seconds = 3600LL;
    else if (range_seconds <= 31LL * 86400LL)
        bucket_seconds = 86400LL;
    const int buckets = bucket_count(start_ts, end_ts, bucket_seconds, 200);
    return build_aggregate_result(mpt_logs, usr_logs, start_ts, end_ts, now_ts, bucket_seconds, buckets, nullptr, true);
}

json StatisticsService::daily(const std::string &date,
                              const json &mpt_logs,
                              const json &usr_logs,
                              long long start_ts,
                              long long end_ts,
                              long long now_ts) const
{
    const long long bucket_seconds = 3600LL;
    const int buckets = 24;
    return build_aggregate_result(mpt_logs, usr_logs, start_ts, end_ts, now_ts, bucket_seconds, buckets, &date, false);
}

json StatisticsService::mountpoint_ranking(const json &mpt_logs,
                                           long long start_ts,
                                           long long end_ts,
                                           long long now_ts,
                                           int limit) const
{
    struct MptStat
    {
        long long total_duration = 0;
        int connections = 0;
        long long last_seen = 0;
        int type_mask = 0;
    };

    std::map<std::string, MptStat> stats;
    for (const auto &[field, entry] : mpt_logs.items())
    {
        (void)field;
        if (!entry.is_object() || !overlaps_range(entry, start_ts, end_ts, now_ts))
            continue;

        const std::string name = entry.value("name", std::string{});
        if (name.empty())
            continue;

        const int type = entry.value("type", 0);
        auto &stat = stats[name];
        stat.total_duration += overlap_duration(entry, start_ts, end_ts, now_ts);
        stat.connections++;
        stat.last_seen = std::max(stat.last_seen, disconnect_or_now(entry, now_ts));
        if (type > 0 && type < 31)
            stat.type_mask |= (1 << type);
    }

    std::vector<std::pair<std::string, MptStat>> sorted(stats.begin(), stats.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.second.total_duration > rhs.second.total_duration;
    });

    json result = json::array();
    int count = 0;
    const int max_count = normalized_limit(limit);
    for (const auto &[name, stat] : sorted)
    {
        if (count >= max_count)
            break;
        json types = json::array();
        if (stat.type_mask & (1 << TYPE_SERVER))
            types.push_back("SERVER");
        if (stat.type_mask & (1 << TYPE_PULL))
            types.push_back("PULL");
        result.push_back(json{{"name", name},
                              {"total_duration", stat.total_duration},
                              {"connections", stat.connections},
                              {"last_seen", stat.last_seen},
                              {"types", types}});
        count++;
    }
    return result;
}

json StatisticsService::user_ranking(const json &usr_logs,
                                     long long start_ts,
                                     long long end_ts,
                                     long long now_ts,
                                     int limit) const
{
    struct UsrStat
    {
        long long total_duration = 0;
        int connections = 0;
        long long last_seen = 0;
        std::set<std::string> mounts;
        int type_mask = 0;
    };

    std::map<std::string, UsrStat> stats;
    for (const auto &[field, entry] : usr_logs.items())
    {
        (void)field;
        if (!entry.is_object() || !overlaps_range(entry, start_ts, end_ts, now_ts))
            continue;

        const std::string name = entry.value("name", std::string{});
        if (name.empty())
            continue;

        const int type = entry.value("type", 0);
        auto &stat = stats[name];
        stat.total_duration += overlap_duration(entry, start_ts, end_ts, now_ts);
        stat.connections++;
        stat.last_seen = std::max(stat.last_seen, disconnect_or_now(entry, now_ts));
        const std::string mount = entry.value("mount", std::string{});
        if (!mount.empty())
            stat.mounts.insert(mount);
        if (type > 0 && type < 31)
            stat.type_mask |= (1 << type);
    }

    std::vector<std::pair<std::string, UsrStat>> sorted(stats.begin(), stats.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.second.total_duration > rhs.second.total_duration;
    });

    json result = json::array();
    int count = 0;
    const int max_count = normalized_limit(limit);
    for (const auto &[name, stat] : sorted)
    {
        if (count >= max_count)
            break;
        json types = json::array();
        if (stat.type_mask & (1 << TYPE_CLIENT))
            types.push_back("CLIENT");
        if (stat.type_mask & (1 << TYPE_NEAREST))
            types.push_back("NEAREST");
        if (stat.type_mask & (1 << TYPE_ALIAS))
            types.push_back("ALIAS");
        if (stat.type_mask & (1 << TYPE_PUSH))
            types.push_back("PUSH");
        result.push_back(json{{"name", name},
                              {"total_duration", stat.total_duration},
                              {"connections", stat.connections},
                              {"last_seen", stat.last_seen},
                              {"mount_count", static_cast<int>(stat.mounts.size())},
                              {"types", types}});
        count++;
    }
    return result;
}

} // namespace navcaster::http_api
