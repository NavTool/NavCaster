#include "domain/rtcm3_parser.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace navcaster::caster {
namespace {

constexpr std::uint8_t kRtcm3Preamble = 0xD3;
constexpr std::size_t kRtcm3MaxPayloadBytes = 1023;

std::uint32_t crc24q(const std::uint8_t *data, std::size_t length)
{
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]) << 16;
        for (int bit = 0; bit < 8; ++bit) {
            crc <<= 1;
            if (crc & 0x1000000U) {
                crc ^= 0x1864CFBU;
            }
        }
    }
    return crc & 0xFFFFFFU;
}

class BitReader {
public:
    BitReader(const std::uint8_t *data, std::size_t length)
        : data_(data), bit_count_(length * 8)
    {
    }

    bool read_unsigned(int bits, std::uint64_t &value)
    {
        if (bits < 0 || bits > 64 || bit_offset_ + static_cast<std::size_t>(bits) > bit_count_) {
            return false;
        }
        value = 0;
        for (int i = 0; i < bits; ++i) {
            const auto byte_index = (bit_offset_ + static_cast<std::size_t>(i)) / 8;
            const auto bit_index = 7 - ((bit_offset_ + static_cast<std::size_t>(i)) % 8);
            value = (value << 1) | ((data_[byte_index] >> bit_index) & 0x01U);
        }
        bit_offset_ += static_cast<std::size_t>(bits);
        return true;
    }

    bool read_signed(int bits, std::int64_t &value)
    {
        std::uint64_t raw = 0;
        if (!read_unsigned(bits, raw) || bits <= 0 || bits >= 63) {
            return false;
        }
        const std::uint64_t sign_bit = 1ULL << (bits - 1);
        if ((raw & sign_bit) != 0) {
            const std::uint64_t full_scale = 1ULL << bits;
            value = static_cast<std::int64_t>(raw) - static_cast<std::int64_t>(full_scale);
        } else {
            value = static_cast<std::int64_t>(raw);
        }
        return true;
    }

private:
    const std::uint8_t *data_ = nullptr;
    std::size_t bit_count_ = 0;
    std::size_t bit_offset_ = 0;
};

std::optional<PositionReport> parse_reference_station_frame(const std::uint8_t *payload, std::size_t length)
{
    BitReader reader(payload, length);
    std::uint64_t message_type = 0;
    std::uint64_t discarded = 0;
    if (!reader.read_unsigned(12, message_type)) {
        return std::nullopt;
    }
    if (message_type != 1005 && message_type != 1006) {
        return std::nullopt;
    }

    std::int64_t ecef_x_raw = 0;
    std::int64_t ecef_y_raw = 0;
    std::int64_t ecef_z_raw = 0;
    if (!reader.read_unsigned(12, discarded) ||
        !reader.read_unsigned(6, discarded) ||
        !reader.read_unsigned(1, discarded) ||
        !reader.read_unsigned(1, discarded) ||
        !reader.read_unsigned(1, discarded) ||
        !reader.read_unsigned(1, discarded) ||
        !reader.read_signed(38, ecef_x_raw) ||
        !reader.read_unsigned(1, discarded) ||
        !reader.read_unsigned(1, discarded) ||
        !reader.read_signed(38, ecef_y_raw) ||
        !reader.read_unsigned(2, discarded) ||
        !reader.read_signed(38, ecef_z_raw)) {
        return std::nullopt;
    }

    if (message_type == 1006) {
        if (!reader.read_unsigned(16, discarded)) {
            return std::nullopt;
        }
    }

    const double x = static_cast<double>(ecef_x_raw) * 0.0001;
    const double y = static_cast<double>(ecef_y_raw) * 0.0001;
    const double z = static_cast<double>(ecef_z_raw) * 0.0001;
    double latitude = 0.0;
    double longitude = 0.0;
    double height = 0.0;
    if (!ecef_to_geodetic(x, y, z, latitude, longitude, height)) {
        return std::nullopt;
    }

    PositionReport report;
    report.source = message_type == 1005 ? PositionSource::Rtcm1005 : PositionSource::Rtcm1006;
    report.message_type = static_cast<std::uint16_t>(message_type);
    report.position.valid = true;
    report.position.ecef_x_m = x;
    report.position.ecef_y_m = y;
    report.position.ecef_z_m = z;
    report.position.latitude_deg = latitude;
    report.position.longitude_deg = longitude;
    report.position.height_m = height;
    return report;
}

} // namespace

std::vector<PositionReport> Rtcm3Parser::feed(const char *data, std::size_t length)
{
    std::vector<PositionReport> reports;
    if (!data || length == 0) {
        return reports;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(data);
    buffer_.insert(buffer_.end(), bytes, bytes + length);

    while (buffer_.size() >= 3) {
        const auto preamble = std::find(buffer_.begin(), buffer_.end(), kRtcm3Preamble);
        if (preamble == buffer_.end()) {
            buffer_.clear();
            break;
        }
        if (preamble != buffer_.begin()) {
            buffer_.erase(buffer_.begin(), preamble);
        }
        if (buffer_.size() < 3) {
            break;
        }
        if ((buffer_[1] & 0xFCU) != 0) {
            buffer_.erase(buffer_.begin());
            continue;
        }

        const std::size_t payload_length =
            (static_cast<std::size_t>(buffer_[1] & 0x03U) << 8U) | static_cast<std::size_t>(buffer_[2]);
        if (payload_length > kRtcm3MaxPayloadBytes) {
            buffer_.erase(buffer_.begin());
            continue;
        }

        const std::size_t frame_length = 3 + payload_length + 3;
        if (buffer_.size() < frame_length) {
            break;
        }

        const std::uint32_t expected_crc =
            (static_cast<std::uint32_t>(buffer_[3 + payload_length]) << 16U) |
            (static_cast<std::uint32_t>(buffer_[3 + payload_length + 1]) << 8U) |
            static_cast<std::uint32_t>(buffer_[3 + payload_length + 2]);
        const std::uint32_t actual_crc = crc24q(buffer_.data(), 3 + payload_length);
        if (expected_crc != actual_crc) {
            buffer_.erase(buffer_.begin());
            continue;
        }

        if (auto report = parse_reference_station_frame(buffer_.data() + 3, payload_length)) {
            reports.push_back(*report);
        }
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_length));
    }

    if (buffer_.size() > kRtcm3MaxPayloadBytes + 6) {
        buffer_.erase(buffer_.begin(), buffer_.end() - static_cast<std::ptrdiff_t>(kRtcm3MaxPayloadBytes + 6));
    }
    return reports;
}

void Rtcm3Parser::reset()
{
    buffer_.clear();
}

} // namespace navcaster::caster
