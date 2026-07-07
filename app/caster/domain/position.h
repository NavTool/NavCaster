#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

enum class PositionSource {
    Unknown,
    NmeaGga,
    Rtcm1005,
    Rtcm1006,
};

struct GeoPosition {
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    double height_m = 0.0;
    double ecef_x_m = 0.0;
    double ecef_y_m = 0.0;
    double ecef_z_m = 0.0;
    std::uint64_t updated_at_ms = 0;
};

struct PositionReport {
    GeoPosition position;
    PositionSource source = PositionSource::Unknown;
    std::uint16_t message_type = 0;
    int quality = 0;
    int satellites = 0;
    double diff_age = 0.0;
};

std::string position_source_name(PositionSource source);
bool geodetic_to_ecef(double latitude_deg, double longitude_deg, double height_m, double &x, double &y, double &z);
bool ecef_to_geodetic(double x, double y, double z, double &latitude_deg, double &longitude_deg, double &height_m);

} // namespace navcaster::caster
