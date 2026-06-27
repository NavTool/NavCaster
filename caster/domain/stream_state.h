#pragma once

#include <cstdint>

namespace navcaster::caster {

struct StreamCounters {
    std::uint64_t bytes_in = 0;
    std::uint64_t bytes_out = 0;
    std::uint64_t messages_in = 0;
    std::uint64_t messages_out = 0;
};

} // namespace navcaster::caster
