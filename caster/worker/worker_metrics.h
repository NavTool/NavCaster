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
    std::uint64_t source_count = 0;
    std::uint64_t client_count = 0;
    std::uint64_t bytes_in = 0;
    std::uint64_t bytes_out = 0;
    std::uint64_t fanout_write_count = 0;
    std::uint64_t redis_publish_count = 0;
    std::uint64_t redis_publish_bytes = 0;
    std::uint64_t slow_client_disconnect_count = 0;
    std::uint64_t output_buffer_limit_count = 0;
    std::uint64_t redis_contexts_reserved = 0;
};

} // namespace navcaster::caster
