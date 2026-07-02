#include "ring_log_service.h"

#include "controller_helpers.h"

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

int minimum_level(const std::string &level)
{
    if (level.empty())
    {
        return 0;
    }
    if (level == "trace")
    {
        return 0;
    }
    if (level == "debug")
    {
        return 1;
    }
    if (level == "info")
    {
        return 2;
    }
    if (level == "warn" || level == "warning")
    {
        return 3;
    }
    if (level == "error" || level == "err")
    {
        return 4;
    }
    if (level == "critical")
    {
        return 5;
    }
    return 6;
}
} // namespace

int infer_ring_log_level(const std::string &line)
{
    if (line.find("[debug]") != std::string::npos)
    {
        return 1;
    }
    if (line.find("[trace]") != std::string::npos)
    {
        return 0;
    }
    if (line.find("[warning]") != std::string::npos || line.find("[warn]") != std::string::npos)
    {
        return 3;
    }
    if (line.find("[error]") != std::string::npos || line.find("[err]") != std::string::npos)
    {
        return 4;
    }
    if (line.find("[critical]") != std::string::npos)
    {
        return 5;
    }
    return 2;
}

std::size_t normalize_ring_log_count(std::size_t count)
{
    if (count == 0 || count > 5000)
    {
        return 500;
    }
    return count;
}

RingLogService::RingLogService(RingLogReader reader)
    : _reader(std::move(reader))
{
}

ControllerResponse RingLogService::list(std::size_t count, const std::string &level, long long timestamp_ms)
{
    const std::size_t normalized_count = normalize_ring_log_count(count);
    const int min_level = minimum_level(level);

    json items = json::array();
    for (const auto &line : _reader(normalized_count))
    {
        const int item_level = infer_ring_log_level(line);
        if (item_level < min_level)
        {
            continue;
        }
        items.push_back(json{{"timestamp", timestamp_ms},
                             {"level", item_level},
                             {"category", "log"},
                             {"message", line}});
    }
    return json_response(200, json{{"items", items}, {"count", items.size()}});
}

} // namespace navcaster::http_api
