#pragma once

#include <algorithm>
#include <cstddef>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace navcaster::auth
{
struct RecordLimitDecision
{
    bool current_allowed = false;
    std::vector<std::string> evicted_connect_keys;
};

inline RecordLimitDecision plan_record_limit(
    const std::multimap<std::time_t, std::string> &records,
    const std::string &current_connect_key,
    int connect_limit,
    bool online_protection)
{
    RecordLimitDecision decision;
    decision.current_allowed = std::find_if(records.begin(), records.end(), [&](const auto &record) {
                                   return record.second == current_connect_key;
                               }) != records.end();
    if (!decision.current_allowed)
    {
        return decision;
    }

    if (connect_limit <= 0)
    {
        decision.current_allowed = false;
        return decision;
    }

    const auto limit = static_cast<std::size_t>(connect_limit);
    if (records.size() <= limit)
    {
        return decision;
    }

    if (online_protection)
    {
        decision.current_allowed = false;
        return decision;
    }

    std::size_t victims_needed = records.size() - limit;
    for (const auto &[timestamp, connect_key] : records)
    {
        (void)timestamp;
        if (connect_key == current_connect_key)
        {
            continue;
        }

        decision.evicted_connect_keys.push_back(connect_key);
        --victims_needed;
        if (victims_needed == 0)
        {
            break;
        }
    }

    if (victims_needed > 0)
    {
        decision.current_allowed = false;
    }
    return decision;
}
}
