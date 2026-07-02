#include "infra/timer.h"

namespace navcaster::caster {

std::uint64_t steady_time_ms()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

} // namespace navcaster::caster
