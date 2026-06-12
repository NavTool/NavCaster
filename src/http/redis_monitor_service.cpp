#include "redis_monitor_service.h"

#include "controller_helpers.h"
#include "redis_monitor_repository.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace navcaster::http_api
{

nlohmann::json parse_redis_info(const std::string &info_text)
{
    nlohmann::json result = nlohmann::json::object();
    std::string current_section;
    std::istringstream stream(info_text);
    std::string line;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }

        if (line.size() > 2 && line[0] == '#')
        {
            current_section = line.substr(2);
            std::transform(current_section.begin(), current_section.end(), current_section.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            result[current_section] = nlohmann::json::object();
            continue;
        }

        auto colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        nlohmann::json parsed_value;
        try
        {
            size_t pos = 0;
            if (value.find('.') != std::string::npos)
            {
                double d = std::stod(value, &pos);
                parsed_value = pos == value.size() ? nlohmann::json(d) : nlohmann::json(value);
            }
            else
            {
                long long ll = std::stoll(value, &pos);
                parsed_value = pos == value.size() ? nlohmann::json(ll) : nlohmann::json(value);
            }
        }
        catch (...)
        {
            parsed_value = value;
        }

        if (!current_section.empty() && result.contains(current_section))
        {
            result[current_section][key] = parsed_value;
        }
        else
        {
            result[key] = parsed_value;
        }
    }
    return result;
}

nlohmann::json redis_monitor_summary_body(const nlohmann::json &info, long long total_keys)
{
    nlohmann::json result = nlohmann::json::object();

    if (info.contains("server"))
    {
        const auto &server = info["server"];
        result["server"] = {
            {"redis_version", server.value("redis_version", "")},
            {"uptime_in_seconds", server.value("uptime_in_seconds", 0)},
            {"tcp_port", server.value("tcp_port", 0)},
            {"os", server.value("os", "")},
            {"process_id", server.value("process_id", 0)}};
    }

    if (info.contains("clients"))
    {
        const auto &clients = info["clients"];
        result["clients"] = {
            {"connected_clients", clients.value("connected_clients", 0)},
            {"blocked_clients", clients.value("blocked_clients", 0)},
            {"maxclients", clients.value("maxclients", 0)}};
    }

    if (info.contains("memory"))
    {
        const auto &memory = info["memory"];
        result["memory"] = {
            {"used_memory", memory.value("used_memory", 0)},
            {"used_memory_human", memory.value("used_memory_human", "")},
            {"used_memory_rss", memory.value("used_memory_rss", 0)},
            {"used_memory_rss_human", memory.value("used_memory_rss_human", "")},
            {"used_memory_peak", memory.value("used_memory_peak", 0)},
            {"used_memory_peak_human", memory.value("used_memory_peak_human", "")},
            {"mem_fragmentation_ratio", memory.value("mem_fragmentation_ratio", 0.0)}};
    }

    if (info.contains("stats"))
    {
        const auto &stats = info["stats"];
        const long long hits = stats.value("keyspace_hits", 0LL);
        const long long misses = stats.value("keyspace_misses", 0LL);
        const double hit_rate = hits + misses > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0;
        result["stats"] = {
            {"total_connections_received", stats.value("total_connections_received", 0)},
            {"total_commands_processed", stats.value("total_commands_processed", 0)},
            {"instantaneous_ops_per_sec", stats.value("instantaneous_ops_per_sec", 0)},
            {"keyspace_hits", hits},
            {"keyspace_misses", misses},
            {"hit_rate", hit_rate},
            {"instantaneous_input_kbps", stats.value("instantaneous_input_kbps", 0.0)},
            {"instantaneous_output_kbps", stats.value("instantaneous_output_kbps", 0.0)}};
    }

    if (info.contains("replication"))
    {
        const auto &replication = info["replication"];
        result["replication"] = {
            {"role", replication.value("role", "")},
            {"connected_slaves", replication.value("connected_slaves", 0)}};
    }

    if (info.contains("keyspace"))
    {
        result["keyspace"] = info["keyspace"];
    }

    result["total_keys"] = total_keys;
    return result;
}

long long redis_monitor_history_limit(const std::string &range)
{
    if (range == "6h")
    {
        return 360;
    }
    if (range == "24h")
    {
        return 1440;
    }
    if (range == "7d")
    {
        return 10080;
    }
    return 60;
}

nlohmann::json redis_monitor_history_items(const nlohmann::json &raw_items)
{
    nlohmann::json items = nlohmann::json::array();
    if (!raw_items.is_array())
    {
        return items;
    }

    for (auto it = raw_items.rbegin(); it != raw_items.rend(); ++it)
    {
        if (!it->is_object() || it->value("t", 0LL) <= 0 || it->value("used_memory", 0ULL) == 0)
        {
            continue;
        }
        items.push_back(*it);
    }
    return items;
}

RedisMonitorService::RedisMonitorService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse RedisMonitorService::summary()
{
    auto info_raw = _redis.info();
    if (info_raw.empty())
    {
        return error_response(503, "Redis not available");
    }
    return json_response(200, redis_monitor_summary_body(parse_redis_info(info_raw), _redis.dbsize()));
}

ControllerResponse RedisMonitorService::history(const std::string &range)
{
    storage::RedisMonitorRepository repo(_redis);
    auto items = redis_monitor_history_items(repo.history(redis_monitor_history_limit(range)));
    return json_response(200, {{"items", items}, {"count", items.size()}});
}

} // namespace navcaster::http_api
