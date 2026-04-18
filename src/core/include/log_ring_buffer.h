#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster_log
{
void install_ring_buffer_sink(std::size_t capacity = 500);
nlohmann::json get_recent_logs(const std::string &min_level = "info", std::size_t limit = 100);
}