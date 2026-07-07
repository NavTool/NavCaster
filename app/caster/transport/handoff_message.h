#pragma once

#include <cstdint>
#include <string>

#include <event2/util.h>

#include "domain/connect_info.h"

namespace navcaster::caster {

struct HandoffMessage {
    evutil_socket_t fd = -1;
    std::string connect_key;
    ConnectInfo connect_info;
    std::string initial_bytes;
    std::string remote_addr;
    std::uint16_t remote_port = 0;
    std::uint64_t accepted_at_ms = 0;
};

} // namespace navcaster::caster
