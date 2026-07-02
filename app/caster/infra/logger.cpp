#include "infra/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace navcaster::caster {
namespace {

std::mutex &log_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::string now_text()
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm tm_value{};
#if defined(_WIN32)
    localtime_s(&tm_value, &seconds);
#else
    localtime_r(&seconds, &tm_value);
#endif

    std::ostringstream out;
    out << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void write_log(const char *level, const std::string &message)
{
    std::lock_guard<std::mutex> lock(log_mutex());
    std::cerr << now_text() << " [" << level << "] " << message << std::endl;
}

} // namespace

void log_info(const std::string &message)
{
    write_log("info", message);
}

void log_warn(const std::string &message)
{
    write_log("warn", message);
}

void log_error(const std::string &message)
{
    write_log("error", message);
}

} // namespace navcaster::caster
