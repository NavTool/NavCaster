#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "domain/connect_info.h"
#include "domain/nmea_gga_parser.h"
#include "domain/position.h"
#include "domain/rtcm3_parser.h"
#include "domain/sourcetable.h"
#include "runtime/runtime_metrics.h"
#include "transport/acceptor_session.h"

namespace {

int g_failures = 0;

void expect_true(bool condition, const std::string &name)
{
    if (!condition) {
        ++g_failures;
        std::cerr << "[caster_basic_smoke] failed: " << name << "\n";
    }
}

void expect_near(double actual, double expected, double tolerance, const std::string &name)
{
    if (std::fabs(actual - expected) > tolerance) {
        ++g_failures;
        std::cerr << "[caster_basic_smoke] failed: " << name << " actual=" << actual << " expected=" << expected
                  << " tolerance=" << tolerance << "\n";
    }
}

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

class BitWriter {
public:
    void write_unsigned(std::uint64_t value, int bits)
    {
        for (int i = bits - 1; i >= 0; --i) {
            if ((bit_offset_ % 8) == 0) {
                data_.push_back(0);
            }
            const auto bit = static_cast<std::uint8_t>((value >> i) & 0x01U);
            data_.back() |= static_cast<std::uint8_t>(bit << (7 - (bit_offset_ % 8)));
            ++bit_offset_;
        }
    }

    void write_signed(std::int64_t value, int bits)
    {
        const std::uint64_t raw =
            value < 0 ? (static_cast<std::uint64_t>(1ULL << bits) + static_cast<std::uint64_t>(value)) :
                        static_cast<std::uint64_t>(value);
        write_unsigned(raw, bits);
    }

    const std::vector<std::uint8_t> &data() const { return data_; }

private:
    std::vector<std::uint8_t> data_;
    int bit_offset_ = 0;
};

std::vector<std::uint8_t> build_rtcm_1005(double latitude, double longitude, double height)
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    navcaster::caster::geodetic_to_ecef(latitude, longitude, height, x, y, z);

    BitWriter payload;
    payload.write_unsigned(1005, 12);
    payload.write_unsigned(1, 12);
    payload.write_unsigned(0, 6);
    payload.write_unsigned(1, 1);
    payload.write_unsigned(1, 1);
    payload.write_unsigned(1, 1);
    payload.write_unsigned(0, 1);
    payload.write_signed(static_cast<std::int64_t>(std::llround(x * 10000.0)), 38);
    payload.write_unsigned(0, 1);
    payload.write_unsigned(0, 1);
    payload.write_signed(static_cast<std::int64_t>(std::llround(y * 10000.0)), 38);
    payload.write_unsigned(0, 2);
    payload.write_signed(static_cast<std::int64_t>(std::llround(z * 10000.0)), 38);

    std::vector<std::uint8_t> frame;
    const auto payload_length = payload.data().size();
    frame.push_back(0xD3);
    frame.push_back(static_cast<std::uint8_t>((payload_length >> 8U) & 0x03U));
    frame.push_back(static_cast<std::uint8_t>(payload_length & 0xFFU));
    frame.insert(frame.end(), payload.data().begin(), payload.data().end());
    const auto crc = crc24q(frame.data(), frame.size());
    frame.push_back(static_cast<std::uint8_t>((crc >> 16U) & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>((crc >> 8U) & 0xFFU));
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFFU));
    return frame;
}

std::vector<std::string> split_semicolon(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    std::istringstream input(line);
    while (std::getline(input, field, ';')) {
        fields.push_back(field);
    }
    return fields;
}

void test_acceptor_parser()
{
    navcaster::caster::AcceptorSessionParser parser;
    auto table = parser.parse_request_head("GET / HTTP/1.0\r\nHost: caster\r\n\r\n");
    expect_true(table.type == navcaster::caster::ConnectType::SourceTable, "GET root is source table");
    expect_true(table.mount.empty(), "source table mount empty");

    auto client = parser.parse_request_head(
        "GET /BASE1 HTTP/1.1\r\nHost: caster\r\nNtrip-GGA: $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n\r\n");
    expect_true(client.type == navcaster::caster::ConnectType::Client, "GET mount is client");
    expect_true(client.mount == "BASE1", "client mount parsed");
    expect_true(!client.initial_gga.empty(), "Ntrip-GGA parsed");

    auto source = parser.parse_request_head("SOURCE pass /BASE2\r\nSource-Agent: test\r\n\r\n");
    expect_true(source.type == navcaster::caster::ConnectType::Source, "SOURCE is source");
    expect_true(source.mount == "BASE2", "SOURCE mount parsed");
}

void test_nmea()
{
    const auto report = navcaster::caster::parse_nmea_gga_sentence(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47");
    expect_true(report.has_value(), "GGA parses");
    if (!report) {
        return;
    }
    expect_near(report->position.latitude_deg, 48.1173, 0.000001, "GGA latitude");
    expect_near(report->position.longitude_deg, 11.516666667, 0.000001, "GGA longitude");
    expect_near(report->position.height_m, 545.4, 0.001, "GGA height");
    expect_true(report->quality == 1, "GGA quality");
    expect_true(report->satellites == 8, "GGA satellites");
}

void test_rtcm()
{
    constexpr double latitude = 48.1173;
    constexpr double longitude = 11.516666667;
    constexpr double height = 545.4;
    const auto frame = build_rtcm_1005(latitude, longitude, height);
    const std::string data(reinterpret_cast<const char *>(frame.data()), frame.size());

    navcaster::caster::Rtcm3Parser parser;
    const auto reports = parser.feed(data);
    expect_true(reports.size() == 1, "RTCM 1005 report count");
    if (reports.empty()) {
        return;
    }
    expect_true(reports[0].message_type == 1005, "RTCM message type");
    expect_near(reports[0].position.latitude_deg, latitude, 0.00001, "RTCM latitude");
    expect_near(reports[0].position.longitude_deg, longitude, 0.00001, "RTCM longitude");
}

void test_sourcetable()
{
    navcaster::caster::SourcetableEntry entry;
    entry.mount = "BASE1";
    entry.identifier = "BASE1";
    entry.position.valid = true;
    entry.position.latitude_deg = 48.1173;
    entry.position.longitude_deg = 11.516666667;

    const auto text = navcaster::caster::build_sourcetable({entry});
    expect_true(text.find("STR;BASE1;BASE1;RTCM 3.3;1074(1),1084(1),1094(1),1124(1)") != std::string::npos,
                "source table STR line");
    expect_true(text.find("ENDSOURCETABLE\r\n") != std::string::npos, "source table terminator");
    const auto line_end = text.find("\r\n");
    const auto fields = split_semicolon(text.substr(0, line_end));
    expect_true(fields.size() >= 19, "source table field count");
    expect_true(fields[11] == "0", "source table NMEA field");
}

void test_metrics_json()
{
    navcaster::caster::RuntimeMetricsSnapshot snapshot;
    snapshot.runtime_id = "test-runtime";
    snapshot.running = true;
    snapshot.worker_count = 1;

    navcaster::caster::WorkerMetricsSnapshot worker;
    worker.worker_id = 1;
    worker.running = true;
    worker.redis_position_report_count = 2;

    navcaster::caster::MountMetricsSnapshot mount;
    mount.worker_id = 1;
    mount.mount = "BASE1";
    mount.source_online = true;
    mount.base_position_source = navcaster::caster::PositionSource::Rtcm1005;
    mount.base_position.valid = true;
    mount.base_position.latitude_deg = 48.1173;
    mount.base_position.longitude_deg = 11.516666667;
    worker.mounts.push_back(mount);

    navcaster::caster::ClientMetricsSnapshot client;
    client.worker_id = 1;
    client.session_id = 7;
    client.member_key = "test-runtime:worker-1:client-7";
    client.mount = "BASE1";
    client.position_source = navcaster::caster::PositionSource::NmeaGga;
    client.position.valid = true;
    client.position.latitude_deg = 48.1173;
    client.position.longitude_deg = 11.516666667;
    worker.clients.push_back(client);
    snapshot.workers.push_back(worker);

    const auto json = navcaster::caster::runtime_metrics_to_json(snapshot);
    expect_true(json.find("\"mounts\":[") != std::string::npos, "metrics mounts array");
    expect_true(json.find("\"clients\":[") != std::string::npos, "metrics clients array");
    expect_true(json.find("\"base_position_source\":\"rtcm_1005\"") != std::string::npos, "metrics base source");
    expect_true(json.find("\"position_source\":\"nmea_gga\"") != std::string::npos, "metrics client source");
}

} // namespace

int main()
{
    test_acceptor_parser();
    test_nmea();
    test_rtcm();
    test_sourcetable();
    test_metrics_json();

    if (g_failures != 0) {
        std::cerr << "[caster_basic_smoke] failures: " << g_failures << "\n";
        return 1;
    }
    std::cout << "[caster_basic_smoke] all checks passed\n";
    return 0;
}
