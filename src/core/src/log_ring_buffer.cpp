#include "log_ring_buffer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>

#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

namespace
{
std::mutex g_ring_mutex;
std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_ring_sink;

spdlog::level::level_enum parse_level(const std::string &value)
{
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "trace") return spdlog::level::trace;
    if (normalized == "debug") return spdlog::level::debug;
    if (normalized == "info") return spdlog::level::info;
    if (normalized == "warn" || normalized == "warning") return spdlog::level::warn;
    if (normalized == "error") return spdlog::level::err;
    if (normalized == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

std::string format_timestamp(const spdlog::log_clock::time_point &time_point)
{
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()) % 1000;
    auto t = spdlog::log_clock::to_time_t(time_point);
    std::tm tm_value{};
    localtime_r(&t, &tm_value);

    std::ostringstream os;
    os << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return os.str();
}
}

namespace navcaster_log
{
void install_ring_buffer_sink(std::size_t capacity)
{
    std::lock_guard<std::mutex> lock(g_ring_mutex);
    if (g_ring_sink)
        return;

    auto logger = spdlog::default_logger();
    if (!logger)
        return;

    g_ring_sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(capacity);
    logger->sinks().push_back(g_ring_sink);
}

nlohmann::json get_recent_logs(const std::string &min_level, std::size_t limit)
{
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
    {
        std::lock_guard<std::mutex> lock(g_ring_mutex);
        sink = g_ring_sink;
    }

    nlohmann::json result = nlohmann::json::array();
    if (!sink)
        return result;

    if (limit == 0)
        limit = 100;
    if (limit > 500)
        limit = 500;

    const auto level = parse_level(min_level);
    auto items = sink->last_raw();
    std::size_t added = 0;

    for (auto iter = items.rbegin(); iter != items.rend() && added < limit; ++iter)
    {
        if (iter->level < level)
            continue;

        const auto level_view = spdlog::level::to_string_view(iter->level);
        nlohmann::json entry = nlohmann::json::object();
        entry["timestamp"] = format_timestamp(iter->time);
        entry["ts"] = std::chrono::duration_cast<std::chrono::milliseconds>(iter->time.time_since_epoch()).count();
        entry["level"] = std::string(level_view.data(), level_view.size());
        entry["logger"] = std::string(iter->logger_name.data(), iter->logger_name.size());
        entry["message"] = std::string(iter->payload.data(), iter->payload.size());
        result.push_back(std::move(entry));
        ++added;
    }

    return result;
}
}