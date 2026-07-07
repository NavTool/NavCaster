#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

enum class ConnectType {
    Unknown,
    SourceTable,
    Source,
    Client,
    Near,
    RelayPull,
    RelayPush,
};

struct ConnectInfo {
    ConnectType type = ConnectType::Unknown;
    std::string mount;
    std::string user;
    std::string auth_header;
    std::string initial_gga;
    std::string http_version;
    std::string ntrip_version;
    std::string remote_addr;
    std::uint16_t remote_port = 0;
    bool ntrip2 = false;
    bool request_body_chunked = false;
    bool accepts_chunked_response = false;
};

std::string connect_type_name(ConnectType type);

} // namespace navcaster::caster
