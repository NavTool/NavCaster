#include "domain/position.h"

#include <cmath>

namespace navcaster::caster {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kWgs84E2 = kWgs84F * (2.0 - kWgs84F);

} // namespace

std::string position_source_name(PositionSource source)
{
    switch (source) {
    case PositionSource::NmeaGga:
        return "nmea_gga";
    case PositionSource::Rtcm1005:
        return "rtcm_1005";
    case PositionSource::Rtcm1006:
        return "rtcm_1006";
    case PositionSource::Unknown:
    default:
        return "unknown";
    }
}

bool geodetic_to_ecef(double latitude_deg, double longitude_deg, double height_m, double &x, double &y, double &z)
{
    if (!std::isfinite(latitude_deg) || !std::isfinite(longitude_deg) || !std::isfinite(height_m) ||
        latitude_deg < -90.0 || latitude_deg > 90.0 || longitude_deg < -180.0 || longitude_deg > 180.0) {
        return false;
    }

    const double lat = latitude_deg * kPi / 180.0;
    const double lon = longitude_deg * kPi / 180.0;
    const double sin_lat = std::sin(lat);
    const double cos_lat = std::cos(lat);
    const double sin_lon = std::sin(lon);
    const double cos_lon = std::cos(lon);
    const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);

    x = (n + height_m) * cos_lat * cos_lon;
    y = (n + height_m) * cos_lat * sin_lon;
    z = (n * (1.0 - kWgs84E2) + height_m) * sin_lat;
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

bool ecef_to_geodetic(double x, double y, double z, double &latitude_deg, double &longitude_deg, double &height_m)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }

    const double p = std::sqrt(x * x + y * y);
    if (p < 1e-9 && std::fabs(z) < 1e-9) {
        return false;
    }

    double lat = std::atan2(z, p * (1.0 - kWgs84E2));
    double height = 0.0;
    for (int i = 0; i < 8; ++i) {
        const double sin_lat = std::sin(lat);
        const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);
        height = p / std::max(1e-12, std::cos(lat)) - n;
        lat = std::atan2(z, p * (1.0 - kWgs84E2 * n / (n + height)));
    }

    const double sin_lat = std::sin(lat);
    const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);
    height = p / std::max(1e-12, std::cos(lat)) - n;

    latitude_deg = lat * 180.0 / kPi;
    longitude_deg = std::atan2(y, x) * 180.0 / kPi;
    height_m = height;
    return std::isfinite(latitude_deg) && std::isfinite(longitude_deg) && std::isfinite(height_m) &&
           latitude_deg >= -90.0 && latitude_deg <= 90.0 &&
           longitude_deg >= -180.0 && longitude_deg <= 180.0;
}

} // namespace navcaster::caster
