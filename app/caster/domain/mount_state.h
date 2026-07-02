#pragma once

#include <cstdint>
#include <string>

namespace navcaster::caster {

struct MountState {
    std::string mount;
    std::uint32_t owner_worker_id = 0;
    bool source_online = false;
    std::uint64_t subscriber_count = 0;
};

} // namespace navcaster::caster
