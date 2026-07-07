#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <event2/event.h>

#include "domain/rtcm3_parser.h"
#include "session/client_session.h"
#include "session/source_session.h"
#include "storage/redis/redis_pubsub.h"
#include "transport/handoff_message.h"
#include "worker/worker_metrics.h"

namespace navcaster::caster {

class WorkerCore {
public:
    WorkerCore(std::uint32_t worker_id, event_base *base, WorkerRedisBoundary *redis_boundary);

    void accept_handoff(HandoffMessage message);
    WorkerMetricsSnapshot snapshot() const;
    void set_draining(bool draining);
    void handle_redis_mount_data(std::string origin_runtime_id, std::string mount, std::string data);
    void handle_redis_error(const std::string &operation);

private:
    struct MountSessions {
        std::uint64_t source_id = 0;
        std::unordered_set<std::uint64_t> client_ids;
        std::uint64_t source_bytes_in = 0;
        std::uint64_t source_rtcm_frame_count = 0;
        std::uint64_t base_position_report_count = 0;
        PositionSource base_position_source = PositionSource::Unknown;
        GeoPosition base_position;
    };

    struct ClientRuntimeState {
        std::string mount;
        std::string remote_addr;
        std::uint16_t remote_port = 0;
        std::string member_key;
        std::string nmea_buffer;
        std::uint64_t position_report_count = 0;
        PositionSource position_source = PositionSource::Unknown;
        GeoPosition position;
    };

    void create_source_locked(HandoffMessage message);
    void create_client_locked(HandoffMessage message);
    void reject_handoff_locked(HandoffMessage &message, const std::string &reason);

    void handle_source_data(std::uint64_t session_id, std::string data);
    void handle_client_data(std::uint64_t session_id, std::string data);
    void handle_source_closed(std::uint64_t session_id);
    void handle_client_closed(std::uint64_t session_id);

    void handle_source_data_locked(std::uint64_t session_id, const std::string &data);
    void handle_client_data_locked(std::uint64_t session_id, const std::string &data);
    void update_mount_position_locked(const std::string &mount, const PositionReport &report);
    void update_client_position_locked(std::uint64_t session_id, const PositionReport &report);
    std::string client_member_key(std::uint64_t session_id) const;
    void fanout_to_mount_clients_locked(
        const std::string &mount,
        const std::string &data,
        bool remote,
        std::uint64_t *write_count,
        std::uint64_t *write_bytes);
    void close_source_locked(std::uint64_t session_id);
    void close_client_locked(std::uint64_t session_id);
    void erase_mount_if_empty_locked(const std::string &mount);
    void detach_source_locked(std::uint64_t session_id);
    void detach_client_locked(std::uint64_t session_id);
    void schedule_deferred_cleanup_locked();
    void run_deferred_cleanup();

    static void on_deferred_cleanup(evutil_socket_t fd, short what, void *arg);

    std::uint32_t worker_id_ = 0;
    event_base *base_ = nullptr;
    WorkerRedisBoundary *redis_boundary_ = nullptr;
    bool draining_ = false;
    std::uint64_t handoff_received_ = 0;
    std::uint64_t next_session_id_ = 1;
    std::uint64_t bytes_in_ = 0;
    std::uint64_t bytes_out_ = 0;
    std::uint64_t fanout_write_count_ = 0;
    std::uint64_t redis_publish_count_ = 0;
    std::uint64_t redis_publish_bytes_ = 0;
    std::uint64_t redis_publish_error_count_ = 0;
    std::uint64_t redis_subscribe_message_count_ = 0;
    std::uint64_t redis_subscribe_bytes_ = 0;
    std::uint64_t redis_remote_fanout_write_count_ = 0;
    std::uint64_t redis_remote_fanout_bytes_ = 0;
    std::uint64_t redis_error_count_ = 0;
    std::uint64_t redis_subscribed_mount_count_ = 0;
    std::uint64_t redis_position_report_count_ = 0;
    std::uint64_t redis_position_report_error_count_ = 0;
    std::uint64_t slow_client_disconnect_count_ = 0;
    std::uint64_t output_buffer_limit_count_ = 0;

    std::unordered_map<std::uint64_t, std::unique_ptr<SourceSession>> sources_;
    std::unordered_map<std::uint64_t, std::unique_ptr<ClientSession>> clients_;
    std::unordered_map<std::string, MountSessions> mounts_;
    std::unordered_map<std::uint64_t, Rtcm3Parser> source_decoders_;
    std::unordered_map<std::uint64_t, ClientRuntimeState> client_states_;
    std::unordered_set<std::uint64_t> pending_source_cleanup_;
    std::unordered_set<std::uint64_t> pending_client_cleanup_;
    mutable std::mutex mutex_;
    bool cleanup_scheduled_ = false;
};

} // namespace navcaster::caster
