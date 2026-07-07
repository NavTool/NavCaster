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
#include "session/server_session.h"
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
        std::string server_connect_key;
        std::unordered_set<std::string> client_connect_keys;
        std::uint64_t server_bytes_in = 0;
        std::uint64_t server_rtcm_frame_count = 0;
        std::uint64_t base_position_report_count = 0;
        PositionSource base_position_source = PositionSource::Unknown;
        GeoPosition base_position;
    };

    struct ClientRuntimeState {
        std::string mount;
        std::string remote_addr;
        std::uint16_t remote_port = 0;
        std::string connect_key;
        std::string nmea_buffer;
        std::uint64_t position_report_count = 0;
        PositionSource position_source = PositionSource::Unknown;
        GeoPosition position;
    };

    void create_server_locked(HandoffMessage message);
    void create_client_locked(HandoffMessage message);
    void reject_handoff_locked(HandoffMessage &message, const std::string &reason);

    void handle_server_data(const std::string &connect_key, std::string data);
    void handle_client_data(const std::string &connect_key, std::string data);
    void handle_server_closed(const std::string &connect_key);
    void handle_client_closed(const std::string &connect_key);

    void handle_server_data_locked(const std::string &connect_key, const std::string &data);
    void handle_client_data_locked(const std::string &connect_key, const std::string &data);
    void update_mount_position_locked(const std::string &mount, const PositionReport &report);
    void update_client_position_locked(const std::string &connect_key, const PositionReport &report);
    std::string ensure_connect_key(HandoffMessage &message) const;
    void fanout_to_mount_clients_locked(
        const std::string &mount,
        const std::string &data,
        bool remote,
        std::uint64_t *write_count,
        std::uint64_t *write_bytes);
    void close_server_locked(const std::string &connect_key);
    void close_client_locked(const std::string &connect_key);
    void erase_mount_if_empty_locked(const std::string &mount);
    void detach_server_locked(const std::string &connect_key);
    void detach_client_locked(const std::string &connect_key);
    void schedule_deferred_cleanup_locked();
    void run_deferred_cleanup();

    static void on_deferred_cleanup(evutil_socket_t fd, short what, void *arg);

    std::uint32_t _worker_id = 0;
    event_base *_base = nullptr;
    WorkerRedisBoundary *_redis_boundary = nullptr;
    bool _draining = false;
    std::uint64_t _handoff_received = 0;
    std::uint64_t _bytes_in = 0;
    std::uint64_t _bytes_out = 0;
    std::uint64_t _fanout_write_count = 0;
    std::uint64_t _redis_publish_count = 0;
    std::uint64_t _redis_publish_bytes = 0;
    std::uint64_t _redis_publish_error_count = 0;
    std::uint64_t _redis_subscribe_message_count = 0;
    std::uint64_t _redis_subscribe_bytes = 0;
    std::uint64_t _redis_remote_fanout_write_count = 0;
    std::uint64_t _redis_remote_fanout_bytes = 0;
    std::uint64_t _redis_error_count = 0;
    std::uint64_t _redis_subscribed_mount_count = 0;
    std::uint64_t _redis_position_report_count = 0;
    std::uint64_t _redis_position_report_error_count = 0;
    std::uint64_t _slow_client_disconnect_count = 0;
    std::uint64_t _output_buffer_limit_count = 0;

    std::unordered_map<std::string, std::unique_ptr<ServerSession>> _servers;
    std::unordered_map<std::string, std::unique_ptr<ClientSession>> _clients;
    std::unordered_map<std::string, MountSessions> _mounts;
    std::unordered_map<std::string, Rtcm3Parser> _server_decoders;
    std::unordered_map<std::string, ClientRuntimeState> _client_states;
    std::unordered_set<std::string> _pending_server_cleanup;
    std::unordered_set<std::string> _pending_client_cleanup;
    mutable std::mutex _mutex;
    bool _cleanup_scheduled = false;
};

} // namespace navcaster::caster
