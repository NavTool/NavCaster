#pragma once

#include <optional>
#include <string>

#include "domain/position.h"

namespace navcaster::caster {

std::optional<PositionReport> parse_nmea_gga_sentence(const std::string &sentence);
std::optional<PositionReport> parse_latest_nmea_gga(const std::string &text);

} // namespace navcaster::caster
