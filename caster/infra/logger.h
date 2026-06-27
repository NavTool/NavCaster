#pragma once

#include <string>

namespace navcaster::caster {

void log_info(const std::string &message);
void log_warn(const std::string &message);
void log_error(const std::string &message);

} // namespace navcaster::caster
