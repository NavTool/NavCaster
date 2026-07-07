#pragma once

#include <string>
#include <vector>

#include "domain/position.h"

namespace navcaster::caster {

struct SourcetableEntry {
    std::string mount;
    std::string identifier;
    std::string format = "RTCM 3.3";
    std::string format_details = "1074(1),1084(1),1094(1),1124(1)";
    std::string carrier = "2";
    std::string nav_system = "GPS+GLO+GAL+BDS";
    std::string network = "SNT";
    std::string country = "XXX";
    std::string nmea_required = "0";
    std::string solution = "0";
    std::string generator = "NavCaster";
    std::string compression = "none";
    std::string authentication = "N";
    std::string fee = "N";
    std::string bitrate = "0";
    std::string misc = "runtime";
    GeoPosition position;
};

std::string build_sourcetable(const std::vector<SourcetableEntry> &entries);

} // namespace navcaster::caster
