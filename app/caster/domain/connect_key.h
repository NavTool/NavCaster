#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

std::string make_connect_key(
    const std::string &runtime_id,
    std::uint64_t accepted_at_ms,
    const std::string &remote_addr,
    std::uint16_t remote_port);

} // namespace navcaster::caster
