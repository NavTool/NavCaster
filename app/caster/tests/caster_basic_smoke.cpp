#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <event2/event.h>
#include <event2/util.h>
#include <nlohmann/json.hpp>

#include "domain/connect_key.h"
#include "domain/connect_info.h"
#include "domain/http_chunked_codec.h"
#include "domain/nmea_gga_parser.h"
#include "domain/position.h"
#include "domain/rtcm3_parser.h"
#include "domain/sourcetable.h"
#include "infra/event_loop.h"
#include "infra/socket_util.h"
#include "runtime/cluster_sourcetable_cache.h"
#include "runtime/runtime_metrics.h"
#include "session/source_session.h"
#include "transport/acceptor_session.h"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

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
            if ((_bit_offset % 8) == 0) {
                _data.push_back(0);
            }
            const auto bit = static_cast<std::uint8_t>((value >> i) & 0x01U);
            _data.back() |= static_cast<std::uint8_t>(bit << (7 - (_bit_offset % 8)));
            ++_bit_offset;
        }
    }

    void write_signed(std::int64_t value, int bits)
    {
        const std::uint64_t raw =
            value < 0 ? (static_cast<std::uint64_t>(1ULL << bits) + static_cast<std::uint64_t>(value)) :
                        static_cast<std::uint64_t>(value);
        write_unsigned(raw, bits);
    }

    const std::vector<std::uint8_t> &data() const { return _data; }

private:
    std::vector<std::uint8_t> _data;
    int _bit_offset = 0;
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

std::string read_socket_payload(evutil_socket_t fd)
{
    std::string result;
    char buffer[4096] = {0};
    evutil_make_socket_nonblocking(fd);
    const int received = recv(fd, buffer, sizeof(buffer), 0);
    if (received > 0) {
        result.append(buffer, static_cast<std::size_t>(received));
    }
    navcaster::caster::close_socket(fd);
    return result;
}

std::string source_session_response(navcaster::caster::ConnectInfo info, const std::string &body)
{
    evutil_socket_t sockets[2] = {-1, -1};
#if defined(_WIN32)
    constexpr int socket_pair_family = AF_INET;
#else
    constexpr int socket_pair_family = AF_UNIX;
#endif
    if (evutil_socketpair(socket_pair_family, SOCK_STREAM, 0, sockets) != 0) {
        expect_true(false, "source session socketpair");
        return {};
    }

    auto base = navcaster::caster::make_event_base();
    expect_true(base != nullptr, "source session event base");
    if (!base) {
        navcaster::caster::close_socket(sockets[0]);
        navcaster::caster::close_socket(sockets[1]);
        return {};
    }

    navcaster::caster::HandoffMessage message;
    message.fd = sockets[0];
    message.connect_info = std::move(info);
    message.connect_key = "test-source-connect";

    bool closed = false;
    const std::string connect_key = message.connect_key;
    navcaster::caster::SourceSession session(
        connect_key,
        std::move(message),
        [&body](const navcaster::caster::ConnectInfo &) {
            return body;
        },
        [&closed, &base, &connect_key](const std::string &closed_connect_key) {
            closed = closed_connect_key == connect_key;
            event_base_loopbreak(base.get());
        });

    const bool started = session.start(base.get());
    expect_true(started, "source session starts");
    if (!started) {
        navcaster::caster::close_socket(sockets[1]);
        return {};
    }

    timeval timeout{};
    timeout.tv_sec = 1;
    event_base_loopexit(base.get(), &timeout);
    event_base_dispatch(base.get());
    expect_true(closed, "source session closes");
    if (!closed) {
        session.close();
    }
    return read_socket_payload(sockets[1]);
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

    auto server = parser.parse_request_head("SOURCE pass /BASE2\r\nSource-Agent: test\r\n\r\n");
    expect_true(server.type == navcaster::caster::ConnectType::Server, "SOURCE is server");
    expect_true(server.mount == "BASE2", "SOURCE mount parsed");

    auto ntrip2_server = parser.parse_request_head(
        "POST /BASE3 HTTP/1.1\r\nHost: caster\r\nNtrip-Version: Ntrip/2.0\r\nTransfer-Encoding: chunked\r\n\r\n");
    expect_true(ntrip2_server.type == navcaster::caster::ConnectType::Server, "NTRIP2 POST is server");
    expect_true(ntrip2_server.mount == "BASE3", "NTRIP2 server mount parsed");
    expect_true(ntrip2_server.ntrip2, "NTRIP2 server version parsed");
    expect_true(ntrip2_server.request_body_chunked, "NTRIP2 server chunked request parsed");

    auto ntrip2_client = parser.parse_request_head(
        "GET /BASE3 HTTP/1.1\r\nHost: caster\r\nNtrip-Version: Ntrip/2.0\r\nTE: chunked\r\n\r\n");
    expect_true(ntrip2_client.type == navcaster::caster::ConnectType::Client, "NTRIP2 GET mount is client");
    expect_true(ntrip2_client.ntrip2, "NTRIP2 client version parsed");
    expect_true(ntrip2_client.accepts_chunked_response, "NTRIP2 client TE chunked parsed");
    expect_true(!ntrip2_client.request_body_chunked, "NTRIP2 client has no chunked request body");
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

void test_http_chunked_codec()
{
    const std::string payload = "abc\r\nxyz";
    const auto encoded = navcaster::caster::encode_http_chunk(payload) + navcaster::caster::encode_http_last_chunk();

    navcaster::caster::HttpChunkedDecoder decoder;
    auto decoded = decoder.feed(encoded.substr(0, 4));
    decoded += decoder.feed(encoded.substr(4));
    expect_true(decoded == payload, "chunked decode payload");
    expect_true(decoder.complete(), "chunked decode complete");
    expect_true(!decoder.failed(), "chunked decode not failed");
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

void test_connect_key()
{
    const auto first = navcaster::caster::make_connect_key("test-runtime", 123, "127.0.0.1", 2101);
    const auto second = navcaster::caster::make_connect_key("test-runtime", 123, "127.0.0.1", 2101);
    expect_true(first != second, "connect key unique");
    expect_true(first.find("test-runtime:conn:") == 0, "connect key prefix");
}

void test_source_session()
{
    const std::string body = "STR;BASE1;BASE1;RTCM 3.3;1074(1),1084(1),1094(1),1124(1);2;GPS;SNIP;CHN;30.00000000;120.00000000;0;0;NavCaster;none;B;N;0;\r\nENDSOURCETABLE\r\n";

    navcaster::caster::ConnectInfo ntrip1_info;
    const auto ntrip1_response = source_session_response(ntrip1_info, body);
    expect_true(ntrip1_response.find("SOURCETABLE 200 OK\r\n") == 0, "source session NTRIP1 status");
    expect_true(ntrip1_response.find("Content-Length: " + std::to_string(body.size())) != std::string::npos,
                "source session NTRIP1 content length");
    expect_true(ntrip1_response.find(body) != std::string::npos, "source session NTRIP1 body");

    navcaster::caster::ConnectInfo ntrip2_info;
    ntrip2_info.ntrip2 = true;
    ntrip2_info.accepts_chunked_response = true;
    const auto ntrip2_response = source_session_response(ntrip2_info, body);
    expect_true(ntrip2_response.find("HTTP/1.1 200 OK\r\n") == 0, "source session NTRIP2 status");
    expect_true(ntrip2_response.find("Transfer-Encoding: chunked\r\n") != std::string::npos,
                "source session NTRIP2 chunked header");

    const auto header_end = ntrip2_response.find("\r\n\r\n");
    expect_true(header_end != std::string::npos, "source session NTRIP2 header end");
    if (header_end != std::string::npos) {
        navcaster::caster::HttpChunkedDecoder decoder;
        const auto decoded = decoder.feed(ntrip2_response.substr(header_end + 4));
        expect_true(decoded == body, "source session NTRIP2 chunked body");
        expect_true(decoder.complete(), "source session NTRIP2 chunked complete");
        expect_true(!decoder.failed(), "source session NTRIP2 chunked not failed");
    }
}

void test_cluster_sourcetable_cache()
{
    navcaster::caster::ClusterSourcetableCache cache("runtime-a");

    navcaster::caster::SourcetableEntry local;
    local.mount = "BASE_A";
    local.identifier = "BASE_A";
    local.position.valid = true;
    local.position.latitude_deg = 30.0;
    local.position.longitude_deg = 120.0;
    cache.upsert_local_entry(local);

    const auto local_json = cache.local_snapshot_json();
    navcaster::caster::ClusterSourcetableCache remote("runtime-b");
    expect_true(remote.apply_remote_snapshot_json(local_json), "cluster sourcetable applies remote snapshot");
    const auto remote_entries = remote.snapshot_entries();
    expect_true(remote_entries.size() == 1, "cluster sourcetable remote entry count");
    expect_true(!remote_entries.empty() && remote_entries[0].mount == "BASE_A", "cluster sourcetable remote mount");

    cache.remove_local_mount("BASE_A");
    const auto empty_entries = cache.snapshot_entries();
    expect_true(empty_entries.empty(), "cluster sourcetable local remove");
    const auto metrics = remote.metrics();
    expect_true(metrics.entry_count == 1, "cluster sourcetable metrics entry count");
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
    worker.server_count = 1;
    worker.client_count = 1;
    worker.sourcetable_request_count = 2;
    worker.active_sessions = 2;
    worker.redis_position_report_count = 2;

    navcaster::caster::MountMetricsSnapshot mount;
    mount.worker_id = 1;
    mount.mount = "BASE1";
    mount.server_online = true;
    mount.server_bytes_in = 128;
    mount.server_rtcm_frame_count = 3;
    mount.base_position_source = navcaster::caster::PositionSource::Rtcm1005;
    mount.base_position.valid = true;
    mount.base_position.latitude_deg = 48.1173;
    mount.base_position.longitude_deg = 11.516666667;
    worker.mounts.push_back(mount);

    navcaster::caster::ClientMetricsSnapshot client;
    client.worker_id = 1;
    client.connect_key = "test-runtime:conn:process:123:7:127.0.0.1:2101";
    client.mount = "BASE1";
    client.position_source = navcaster::caster::PositionSource::NmeaGga;
    client.position.valid = true;
    client.position.latitude_deg = 48.1173;
    client.position.longitude_deg = 11.516666667;
    worker.clients.push_back(client);
    snapshot.workers.push_back(worker);
    snapshot.sourcetable_cache_entry_count = 1;
    snapshot.sourcetable_cache_age_ms = 12;

    const auto metrics_text = navcaster::caster::runtime_metrics_to_json(snapshot);
    const auto metrics = nlohmann::json::parse(metrics_text);
    expect_true(metrics["mounts"].is_array(), "metrics mounts array");
    expect_true(metrics["clients"].is_array(), "metrics clients array");
    expect_true(metrics["mounts"].size() == 1, "metrics mount count");
    expect_true(metrics["clients"].size() == 1, "metrics client count");
    expect_true(metrics["server_count"] == 1, "metrics server count");
    expect_true(metrics["client_count"] == 1, "metrics client count total");
    expect_true(metrics["sourcetable_request_count"] == 2, "metrics sourcetable request count");
    expect_true(metrics["sourcetable_cache_entry_count"] == 1, "metrics sourcetable cache entry count");
    expect_true(metrics["sourcetable_cache_age_ms"] == 12, "metrics sourcetable cache age");
    expect_true(metrics["workers"][0]["server_count"] == 1, "metrics worker server count");
    expect_true(metrics["workers"][0]["client_count"] == 1, "metrics worker client count");
    expect_true(metrics["workers"][0]["sourcetable_request_count"] == 2, "metrics worker sourcetable request count");
    expect_true(metrics["mounts"][0]["server_online"], "metrics mount server online");
    expect_true(metrics["mounts"][0]["server_bytes_in"] == 128, "metrics mount server bytes in");
    expect_true(metrics["mounts"][0]["server_rtcm_frame_count"] == 3, "metrics mount server rtcm count");
    expect_true(metrics["mounts"][0]["base_position_source"] == "rtcm_1005", "metrics base source");
    expect_true(metrics["clients"][0]["connect_key"] == "test-runtime:conn:process:123:7:127.0.0.1:2101",
                "metrics client connect key");
    const auto legacy_session_key = std::string("session") + "_id";
    const auto legacy_member_field = std::string("member") + "_key";
    expect_true(!metrics["clients"][0].contains(legacy_session_key), "metrics client no legacy session key");
    expect_true(!metrics["clients"][0].contains(legacy_member_field), "metrics client no legacy member key");
    expect_true(metrics["clients"][0]["position_source"] == "nmea_gga", "metrics client source");
}

} // namespace

int main()
{
    test_acceptor_parser();
    test_nmea();
    test_rtcm();
    test_http_chunked_codec();
    test_sourcetable();
    test_connect_key();
    test_source_session();
    test_cluster_sourcetable_cache();
    test_metrics_json();

    if (g_failures != 0) {
        std::cerr << "[caster_basic_smoke] failures: " << g_failures << "\n";
        return 1;
    }
    std::cout << "[caster_basic_smoke] all checks passed\n";
    return 0;
}
