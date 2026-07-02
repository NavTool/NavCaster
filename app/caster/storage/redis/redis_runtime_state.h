#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

struct RedisRuntimeStateSnapshot {
    std::string runtime_id;
    std::uint64_t heartbeat_count = 0;
};

} // namespace navcaster::caster
