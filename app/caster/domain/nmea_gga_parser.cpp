#include "domain/nmea_gga_parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace navcaster::caster {
namespace {

std::string trim_sentence(std::string value)
{
    while (!value.empty() && (value.front() == '\r' || value.front() == '\n' || value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

std::vector<std::string> split_fields(std::string sentence)
{
    const auto checksum = sentence.find('*');
    if (checksum != std::string::npos) {
        sentence = sentence.substr(0, checksum);
    }
    std::vector<std::string> fields;
    std::string field;
    std::istringstream input(sentence);
    while (std::getline(input, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

bool parse_double(const std::string &text, double &value)
{
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end && *end == '\0' && std::isfinite(value);
}

bool parse_int(const std::string &text, int &value)
{
    if (text.empty()) {
        return false;
    }
    char *end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool parse_degrees_minutes(const std::string &value, const std::string &hemisphere, bool longitude, double &degrees)
{
    double raw = 0.0;
    if (!parse_double(value, raw) || raw <= 0.0) {
        return false;
    }

    const double deg_part = std::floor(raw / 100.0);
    const double minutes = raw - deg_part * 100.0;
    if (minutes < 0.0 || minutes >= 60.0) {
        return false;
    }

    degrees = deg_part + minutes / 60.0;
    if (hemisphere == "S" || hemisphere == "s" || hemisphere == "W" || hemisphere == "w") {
        degrees = -degrees;
    } else if (!(hemisphere == "N" || hemisphere == "n" || hemisphere == "E" || hemisphere == "e")) {
        return false;
    }

    const double limit = longitude ? 180.0 : 90.0;
    return degrees >= -limit && degrees <= limit;
}

bool is_gga_header(const std::string &header)
{
    if (header.size() < 6 || header.front() != '$') {
        return false;
    }
    std::string normalized = header;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return normalized.size() >= 6 && normalized.substr(normalized.size() - 3) == "GGA";
}

} // namespace

std::optional<PositionReport> parse_nmea_gga_sentence(const std::string &sentence)
{
    const std::string trimmed = trim_sentence(sentence);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    const auto dollar = trimmed.find('$');
    if (dollar == std::string::npos) {
        return std::nullopt;
    }
    const auto fields = split_fields(trimmed.substr(dollar));
    if (fields.size() < 10 || !is_gga_header(fields[0])) {
        return std::nullopt;
    }

    int quality = 0;
    int satellites = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    if (!parse_int(fields[6], quality) || quality <= 0 ||
        !parse_int(fields[7], satellites) ||
        !parse_degrees_minutes(fields[2], fields[3], false, latitude) ||
        !parse_degrees_minutes(fields[4], fields[5], true, longitude) ||
        !parse_double(fields[9], altitude)) {
        return std::nullopt;
    }

    PositionReport report;
    report.source = PositionSource::NmeaGga;
    report.quality = quality;
    report.satellites = satellites;
    report.position.valid = true;
    report.position.latitude_deg = latitude;
    report.position.longitude_deg = longitude;
    report.position.height_m = altitude;
    if (!geodetic_to_ecef(latitude, longitude, altitude, report.position.ecef_x_m, report.position.ecef_y_m, report.position.ecef_z_m)) {
        return std::nullopt;
    }
    return report;
}

std::optional<PositionReport> parse_latest_nmea_gga(const std::string &text)
{
    std::optional<PositionReport> latest;
    std::size_t offset = 0;
    while (true) {
        const auto dollar = text.find('$', offset);
        if (dollar == std::string::npos) {
            break;
        }
        auto end = text.find_first_of("\r\n", dollar);
        if (end == std::string::npos) {
            end = text.size();
        }
        const auto candidate = text.substr(dollar, end - dollar);
        if (auto parsed = parse_nmea_gga_sentence(candidate)) {
            latest = parsed;
        }
        offset = dollar + 1;
    }
    return latest;
}

} // namespace navcaster::caster
