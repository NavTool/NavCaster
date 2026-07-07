#include "domain/sourcetable.h"

#include <iomanip>
#include <sstream>

namespace navcaster::caster {
namespace {

std::string format_coord(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

const std::string &identifier_for(const SourcetableEntry &entry)
{
    return entry.identifier.empty() ? entry.mount : entry.identifier;
}

} // namespace

std::string build_sourcetable(const std::vector<SourcetableEntry> &entries)
{
    std::ostringstream out;
    for (const auto &entry : entries) {
        if (entry.mount.empty()) {
            continue;
        }
        const double latitude = entry.position.valid ? entry.position.latitude_deg : 0.0;
        const double longitude = entry.position.valid ? entry.position.longitude_deg : 0.0;
        out << "STR;"
            << entry.mount << ";"
            << identifier_for(entry) << ";"
            << entry.format << ";"
            << entry.format_details << ";"
            << entry.carrier << ";"
            << entry.nav_system << ";"
            << entry.network << ";"
            << entry.country << ";"
            << format_coord(latitude) << ";"
            << format_coord(longitude) << ";"
            << entry.nmea_required << ";"
            << entry.solution << ";"
            << entry.generator << ";"
            << entry.compression << ";"
            << entry.authentication << ";"
            << entry.fee << ";"
            << entry.bitrate << ";"
            << entry.misc << ";\r\n";
    }
    out << "ENDSOURCETABLE\r\n";
    return out.str();
}

} // namespace navcaster::caster
