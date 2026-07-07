#include "infra/logger.h"

#include <memory>

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace navcaster::caster {
namespace {

std::shared_ptr<spdlog::logger> create_logger()
{
    if (auto logger = spdlog::get("navcaster-caster")) {
        return logger;
    }
    auto logger = spdlog::stderr_logger_mt("navcaster-caster");
    logger->set_pattern("%Y-%m-%d %H:%M:%S [%l] %v");
    logger->flush_on(spdlog::level::warn);
    return logger;
}

std::shared_ptr<spdlog::logger> &logger()
{
    static auto instance = create_logger();
    return instance;
}

} // namespace

void log_info(const std::string &message)
{
    logger()->info("{}", message);
}

void log_warn(const std::string &message)
{
    logger()->warn("{}", message);
}

void log_error(const std::string &message)
{
    logger()->error("{}", message);
}

} // namespace navcaster::caster
