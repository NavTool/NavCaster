#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "domain/position.h"

namespace navcaster::caster {

struct MountMetricsSnapshot {
    std::uint32_t worker_id = 0;
    std::string mount;
    bool server_online = false;
    std::uint64_t client_count = 0;
    std::uint64_t server_bytes_in = 0;
    std::uint64_t server_rtcm_frame_count = 0;
    std::uint64_t base_position_report_count = 0;
    PositionSource base_position_source = PositionSource::Unknown;
    GeoPosition base_position;
};

struct ClientMetricsSnapshot {
    std::uint32_t worker_id = 0;
    std::uint64_t session_id = 0;
    std::string member_key;
    std::string mount;
    std::string remote_addr;
    std::uint16_t remote_port = 0;
    std::uint64_t position_report_count = 0;
    PositionSource position_source = PositionSource::Unknown;
    GeoPosition position;
};

struct WorkerMetricsSnapshot {
    std::uint32_t worker_id = 0;
    bool running = false;
    bool draining = false;
    std::uint64_t mailbox_messages = 0;
    std::uint64_t handoff_received = 0;
    std::uint64_t active_sessions = 0;
    std::uint64_t active_mounts = 0;
    std::uint64_t server_count = 0;
    std::uint64_t client_count = 0;
    std::uint64_t bytes_in = 0;
    std::uint64_t bytes_out = 0;
    std::uint64_t fanout_write_count = 0;
    std::uint64_t redis_publish_count = 0;
    std::uint64_t redis_publish_bytes = 0;
    std::uint64_t redis_publish_error_count = 0;
    std::uint64_t redis_subscribe_message_count = 0;
    std::uint64_t redis_subscribe_bytes = 0;
    std::uint64_t redis_remote_fanout_write_count = 0;
    std::uint64_t redis_remote_fanout_bytes = 0;
    std::uint64_t redis_error_count = 0;
    std::uint64_t redis_subscribed_mount_count = 0;
    std::uint64_t redis_position_report_count = 0;
    std::uint64_t redis_position_report_error_count = 0;
    std::uint64_t slow_client_disconnect_count = 0;
    std::uint64_t output_buffer_limit_count = 0;
    bool redis_connected = false;
    std::uint64_t redis_contexts_reserved = 0;
    std::vector<MountMetricsSnapshot> mounts;
    std::vector<ClientMetricsSnapshot> clients;
};

} // namespace navcaster::caster
