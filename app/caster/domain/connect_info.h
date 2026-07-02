#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

enum class ConnectType {
    Unknown,
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
    std::string remote_addr;
    std::uint16_t remote_port = 0;
};

std::string connect_type_name(ConnectType type);

} // namespace navcaster::caster
