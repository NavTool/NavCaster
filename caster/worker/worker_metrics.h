#pragma once

#include <cstdint>

namespace navcaster::caster {

struct WorkerMetricsSnapshot {
    std::uint32_t worker_id = 0;
    bool running = false;
    bool draining = false;
    std::uint64_t mailbox_messages = 0;
    std::uint64_t handoff_received = 0;
    std::uint64_t active_sessions = 0;
    std::uint64_t active_mounts = 0;
    std::uint64_t redis_contexts_reserved = 0;
};

} // namespace navcaster::caster
