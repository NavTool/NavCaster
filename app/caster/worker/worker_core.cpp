#include "worker/worker_core.h"

#include <algorithm>
#include <vector>

#include "domain/connect_key.h"
#include "domain/connect_info.h"
#include "domain/nmea_gga_parser.h"
#include "infra/logger.h"
#include "infra/socket_util.h"
#include "infra/timer.h"

namespace navcaster::caster {
namespace {

constexpr std::size_t kMaxClientOutputBufferBytes = 1024 * 1024;

} // namespace

WorkerCore::WorkerCore(std::uint32_t worker_id, event_base *base, WorkerRedisBoundary *redis_boundary)
    : _worker_id(worker_id), _base(base), _redis_boundary(redis_boundary)
{
}

void WorkerCore::accept_handoff(HandoffMessage message)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ++_handoff_received;

    if (!_base || message.fd < 0 || message.connect_info.mount.empty() || message.connect_info.type == ConnectType::Unknown) {
        reject_handoff_locked(message, "invalid_handoff");
        return;
    }

    switch (message.connect_info.type) {
    case ConnectType::Server:
        create_server_locked(std::move(message));
        break;
    case ConnectType::Client:
        create_client_locked(std::move(message));
        break;
    default:
        reject_handoff_locked(message, "unsupported_connect_type");
        break;
    }
}

WorkerMetricsSnapshot WorkerCore::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    WorkerMetricsSnapshot snapshot;
    snapshot.worker_id = _worker_id;
    snapshot.draining = _draining;
    snapshot.handoff_received = _handoff_received;
    snapshot.server_count = static_cast<std::uint64_t>(_servers.size());
    snapshot.client_count = static_cast<std::uint64_t>(_clients.size());
    snapshot.active_sessions = snapshot.server_count + snapshot.client_count;
    snapshot.active_mounts = static_cast<std::uint64_t>(_mounts.size());
    snapshot.bytes_in = _bytes_in;
    snapshot.bytes_out = _bytes_out;
    snapshot.fanout_write_count = _fanout_write_count;
    snapshot.redis_publish_count = _redis_publish_count;
    snapshot.redis_publish_bytes = _redis_publish_bytes;
    snapshot.redis_publish_error_count = _redis_publish_error_count;
    snapshot.redis_subscribe_message_count = _redis_subscribe_message_count;
    snapshot.redis_subscribe_bytes = _redis_subscribe_bytes;
    snapshot.redis_remote_fanout_write_count = _redis_remote_fanout_write_count;
    snapshot.redis_remote_fanout_bytes = _redis_remote_fanout_bytes;
    snapshot.redis_error_count = _redis_error_count;
    snapshot.redis_subscribed_mount_count = _redis_subscribed_mount_count;
    snapshot.redis_position_report_count = _redis_position_report_count;
    snapshot.redis_position_report_error_count = _redis_position_report_error_count;
    snapshot.slow_client_disconnect_count = _slow_client_disconnect_count;
    snapshot.output_buffer_limit_count = _output_buffer_limit_count;
    snapshot.redis_connected = _redis_boundary && _redis_boundary->connected();
    snapshot.redis_contexts_reserved = 2;
    snapshot.mounts.reserve(_mounts.size());
    for (const auto &item : _mounts) {
        const auto &state = item.second;
        MountMetricsSnapshot mount;
        mount.worker_id = _worker_id;
        mount.mount = item.first;
        mount.server_online = !state.server_connect_key.empty();
        mount.client_count = static_cast<std::uint64_t>(state.client_connect_keys.size());
        mount.server_bytes_in = state.server_bytes_in;
        mount.server_rtcm_frame_count = state.server_rtcm_frame_count;
        mount.base_position_report_count = state.base_position_report_count;
        mount.base_position_source = state.base_position_source;
        mount.base_position = state.base_position;
        snapshot.mounts.push_back(std::move(mount));
    }
    snapshot.clients.reserve(_client_states.size());
    for (const auto &item : _client_states) {
        const auto &state = item.second;
        ClientMetricsSnapshot client;
        client.worker_id = _worker_id;
        client.connect_key = state.connect_key;
        client.mount = state.mount;
        client.remote_addr = state.remote_addr;
        client.remote_port = state.remote_port;
        client.position_report_count = state.position_report_count;
        client.position_source = state.position_source;
        client.position = state.position;
        snapshot.clients.push_back(std::move(client));
    }
    return snapshot;
}

void WorkerCore::set_draining(bool draining)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _draining = draining;
}

void WorkerCore::create_server_locked(HandoffMessage message)
{
    const std::string mount = message.connect_info.mount;

    const auto existing_server_connect_key = _mounts[mount].server_connect_key;
    if (!existing_server_connect_key.empty()) {
        close_server_locked(existing_server_connect_key);
    }

    const std::string connect_key = ensure_connect_key(message);
    auto session = std::make_unique<ServerSession>(
        connect_key,
        _worker_id,
        std::move(message),
        [this](const std::string &server_connect_key, std::string data) {
            handle_server_data(server_connect_key, std::move(data));
        },
        [this](const std::string &server_connect_key) {
            handle_server_closed(server_connect_key);
        });

    if (!session->start(_base)) {
        log_warn("worker " + std::to_string(_worker_id) + " failed to start server session mount=" + mount);
        erase_mount_if_empty_locked(mount);
        return;
    }

    auto &mount_state = _mounts[mount];
    mount_state.server_connect_key = connect_key;
    _server_decoders[connect_key] = Rtcm3Parser{};
    _servers[connect_key] = std::move(session);
    const auto initial_payload = _servers[connect_key]->consume_initial_bytes();
    if (!initial_payload.empty()) {
        handle_server_data_locked(connect_key, initial_payload);
    }
    if (_servers.find(connect_key) != _servers.end() &&
        (_servers[connect_key]->input_failed() || _servers[connect_key]->input_complete())) {
        close_server_locked(connect_key);
    }
}

void WorkerCore::create_client_locked(HandoffMessage message)
{
    const std::string mount = message.connect_info.mount;
    const std::string initial_gga = message.connect_info.initial_gga;
    const std::string connect_key = ensure_connect_key(message);
    auto session = std::make_unique<ClientSession>(
        connect_key,
        _worker_id,
        std::move(message),
        [this](const std::string &client_connect_key, std::string data) {
            handle_client_data(client_connect_key, std::move(data));
        },
        [this](const std::string &client_connect_key) {
            handle_client_closed(client_connect_key);
        });

    if (!session->start(_base)) {
        log_warn("worker " + std::to_string(_worker_id) + " failed to start client session mount=" + mount);
        return;
    }

    auto &mount_state = _mounts[mount];
    const bool should_subscribe = mount_state.client_connect_keys.empty();
    mount_state.client_connect_keys.insert(connect_key);
    ClientRuntimeState client_state;
    client_state.connect_key = connect_key;
    client_state.mount = mount;
    client_state.remote_addr = session->remote_addr();
    client_state.remote_port = session->remote_port();
    _client_states[connect_key] = std::move(client_state);
    _clients[connect_key] = std::move(session);
    if (!initial_gga.empty()) {
        handle_client_data_locked(connect_key, initial_gga);
    }
    if (_clients.find(connect_key) != _clients.end()) {
        const auto initial_payload = _clients[connect_key]->consume_initial_bytes();
        if (!initial_payload.empty()) {
            handle_client_data_locked(connect_key, initial_payload);
        }
    }
    if (_clients.find(connect_key) != _clients.end() &&
        (_clients[connect_key]->input_failed() || _clients[connect_key]->input_complete())) {
        close_client_locked(connect_key);
    }
    if (_clients.find(connect_key) == _clients.end()) {
        return;
    }
    if (_redis_boundary && should_subscribe) {
        if (_redis_boundary->subscribe_mount(mount)) {
            ++_redis_subscribed_mount_count;
        } else {
            ++_redis_error_count;
        }
    }
}

void WorkerCore::reject_handoff_locked(HandoffMessage &message, const std::string &reason)
{
    if (message.fd >= 0) {
        close_socket(message.fd);
        message.fd = -1;
    }
    log_warn("worker " + std::to_string(_worker_id) + " rejected handoff reason=" + reason +
             " type=" + connect_type_name(message.connect_info.type) + " mount=" + message.connect_info.mount);
}

void WorkerCore::handle_server_data(const std::string &connect_key, std::string data)
{
    std::lock_guard<std::mutex> lock(_mutex);
    handle_server_data_locked(connect_key, data);
}

void WorkerCore::handle_client_data(const std::string &connect_key, std::string data)
{
    std::lock_guard<std::mutex> lock(_mutex);
    handle_client_data_locked(connect_key, data);
}

void WorkerCore::handle_server_closed(const std::string &connect_key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    detach_server_locked(connect_key);
    _pending_server_cleanup.insert(connect_key);
    schedule_deferred_cleanup_locked();
}

void WorkerCore::handle_client_closed(const std::string &connect_key)
{
    std::lock_guard<std::mutex> lock(_mutex);
    detach_client_locked(connect_key);
    _pending_client_cleanup.insert(connect_key);
    schedule_deferred_cleanup_locked();
}

void WorkerCore::handle_redis_mount_data(std::string origin_runtime_id, std::string mount, std::string data)
{
    (void)origin_runtime_id;
    std::lock_guard<std::mutex> lock(_mutex);
    if (mount.empty() || data.empty()) {
        return;
    }
    ++_redis_subscribe_message_count;
    _redis_subscribe_bytes += static_cast<std::uint64_t>(data.size());

    std::uint64_t writes = 0;
    std::uint64_t bytes = 0;
    fanout_to_mount_clients_locked(mount, data, true, &writes, &bytes);
    _bytes_out += bytes;
    _redis_remote_fanout_write_count += writes;
    _redis_remote_fanout_bytes += bytes;
}

void WorkerCore::handle_redis_error(const std::string &operation)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ++_redis_error_count;
    if (operation == "publish") {
        ++_redis_publish_error_count;
    }
}

void WorkerCore::handle_server_data_locked(const std::string &connect_key, const std::string &data)
{
    const auto server_it = _servers.find(connect_key);
    if (server_it == _servers.end() || data.empty()) {
        return;
    }

    const std::string mount = server_it->second->mount();
    _bytes_in += static_cast<std::uint64_t>(data.size());
    auto &mount_state = _mounts[mount];
    mount_state.server_bytes_in += static_cast<std::uint64_t>(data.size());

    auto decoder_it = _server_decoders.find(connect_key);
    if (decoder_it != _server_decoders.end()) {
        const auto reports = decoder_it->second.feed(data);
        mount_state.server_rtcm_frame_count += static_cast<std::uint64_t>(reports.size());
        for (const auto &report : reports) {
            update_mount_position_locked(mount, report);
        }
    }

    std::uint64_t writes = 0;
    std::uint64_t bytes = 0;
    fanout_to_mount_clients_locked(mount, data, false, &writes, &bytes);
    _fanout_write_count += writes;
    _bytes_out += bytes;

    ++_redis_publish_count;
    _redis_publish_bytes += static_cast<std::uint64_t>(data.size());
    if (_redis_boundary && !_redis_boundary->publish_mount_data(mount, data)) {
        ++_redis_publish_error_count;
        ++_redis_error_count;
    }
}

void WorkerCore::handle_client_data_locked(const std::string &connect_key, const std::string &data)
{
    if (data.empty()) {
        return;
    }
    auto state_it = _client_states.find(connect_key);
    if (state_it == _client_states.end()) {
        return;
    }

    auto &state = state_it->second;
    state.nmea_buffer += data;
    if (state.nmea_buffer.size() > 8192) {
        state.nmea_buffer.erase(0, state.nmea_buffer.size() - 8192);
    }

    if (auto report = parse_latest_nmea_gga(state.nmea_buffer)) {
        update_client_position_locked(connect_key, *report);
    }

    const auto last_line_break = state.nmea_buffer.find_last_of("\r\n");
    if (last_line_break != std::string::npos) {
        state.nmea_buffer.erase(0, last_line_break + 1);
    }
}

void WorkerCore::update_mount_position_locked(const std::string &mount, const PositionReport &report)
{
    if (mount.empty() || !report.position.valid) {
        return;
    }
    auto &state = _mounts[mount];
    state.base_position = report.position;
    state.base_position.updated_at_ms = steady_time_ms();
    state.base_position_source = report.source;
    ++state.base_position_report_count;

    if (_redis_boundary) {
        if (_redis_boundary->report_mount_position(mount, state.base_position)) {
            ++_redis_position_report_count;
        } else {
            ++_redis_position_report_error_count;
            ++_redis_error_count;
        }
    }
}

void WorkerCore::update_client_position_locked(const std::string &connect_key, const PositionReport &report)
{
    if (!report.position.valid) {
        return;
    }
    auto state_it = _client_states.find(connect_key);
    if (state_it == _client_states.end()) {
        return;
    }

    auto &state = state_it->second;
    state.position = report.position;
    state.position.updated_at_ms = steady_time_ms();
    state.position_source = report.source;
    ++state.position_report_count;

    if (_redis_boundary) {
        if (_redis_boundary->report_client_position(state.connect_key, state.position)) {
            ++_redis_position_report_count;
        } else {
            ++_redis_position_report_error_count;
            ++_redis_error_count;
        }
    }
}

std::string WorkerCore::ensure_connect_key(HandoffMessage &message) const
{
    if (!message.connect_key.empty()) {
        return message.connect_key;
    }
    const std::string runtime_id = _redis_boundary ? _redis_boundary->runtime_id() : std::string("local-runtime");
    message.connect_key = make_connect_key(runtime_id, message.accepted_at_ms, message.remote_addr, message.remote_port);
    return message.connect_key;
}

void WorkerCore::fanout_to_mount_clients_locked(
    const std::string &mount,
    const std::string &data,
    bool remote,
    std::uint64_t *write_count,
    std::uint64_t *write_bytes)
{
    auto mount_it = _mounts.find(mount);
    if (mount_it == _mounts.end()) {
        return;
    }
    std::vector<std::string> client_connect_keys;
    client_connect_keys.reserve(mount_it->second.client_connect_keys.size());
    for (const auto &client_connect_key : mount_it->second.client_connect_keys) {
        client_connect_keys.push_back(client_connect_key);
    }

    for (const auto &client_connect_key : client_connect_keys) {
        auto client_it = _clients.find(client_connect_key);
        if (client_it == _clients.end()) {
            continue;
        }
        ClientSession *client = client_it->second.get();
        if (client->mount() != mount) {
            continue;
        }
        if (client->pending_output_bytes() + data.size() > kMaxClientOutputBufferBytes) {
            ++_slow_client_disconnect_count;
            ++_output_buffer_limit_count;
            close_client_locked(client_connect_key);
            continue;
        }
        if (!client->write_bytes(data.data(), data.size())) {
            close_client_locked(client_connect_key);
            continue;
        }
        if (write_bytes) {
            *write_bytes += static_cast<std::uint64_t>(data.size());
        }
        if (write_count) {
            ++(*write_count);
        }
    }
    if (remote) {
        return;
    }
}

void WorkerCore::close_server_locked(const std::string &connect_key)
{
    auto server_it = _servers.find(connect_key);
    if (server_it == _servers.end()) {
        return;
    }

    detach_server_locked(connect_key);
    _servers.erase(server_it);
}

void WorkerCore::close_client_locked(const std::string &connect_key)
{
    auto client_it = _clients.find(connect_key);
    if (client_it == _clients.end()) {
        return;
    }

    detach_client_locked(connect_key);
    _clients.erase(client_it);
}

void WorkerCore::erase_mount_if_empty_locked(const std::string &mount)
{
    auto mount_it = _mounts.find(mount);
    if (mount_it == _mounts.end()) {
        return;
    }
    if (mount_it->second.server_connect_key.empty() && mount_it->second.client_connect_keys.empty()) {
        _mounts.erase(mount_it);
    }
}

void WorkerCore::detach_server_locked(const std::string &connect_key)
{
    auto server_it = _servers.find(connect_key);
    if (server_it == _servers.end()) {
        return;
    }

    const std::string mount = server_it->second->mount();
    auto mount_it = _mounts.find(mount);
    if (mount_it != _mounts.end() && mount_it->second.server_connect_key == connect_key) {
        mount_it->second.server_connect_key.clear();
    }
    _server_decoders.erase(connect_key);
    erase_mount_if_empty_locked(mount);
}

void WorkerCore::detach_client_locked(const std::string &connect_key)
{
    auto client_it = _clients.find(connect_key);
    if (client_it == _clients.end()) {
        return;
    }

    const std::string mount = client_it->second->mount();
    auto mount_it = _mounts.find(mount);
    if (mount_it != _mounts.end()) {
        mount_it->second.client_connect_keys.erase(connect_key);
        if (mount_it->second.client_connect_keys.empty() && _redis_boundary) {
            if (!_redis_boundary->unsubscribe_mount(mount)) {
                ++_redis_error_count;
            }
            if (_redis_subscribed_mount_count > 0) {
                --_redis_subscribed_mount_count;
            }
        }
    }
    _client_states.erase(connect_key);
    erase_mount_if_empty_locked(mount);
}

void WorkerCore::schedule_deferred_cleanup_locked()
{
    if (_cleanup_scheduled || !_base) {
        return;
    }

    _cleanup_scheduled = event_base_once(_base, -1, EV_TIMEOUT, &WorkerCore::on_deferred_cleanup, this, nullptr) == 0;
}

void WorkerCore::run_deferred_cleanup()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _cleanup_scheduled = false;
    for (const auto &connect_key : _pending_server_cleanup) {
        _servers.erase(connect_key);
    }
    _pending_server_cleanup.clear();
    for (const auto &connect_key : _pending_client_cleanup) {
        _clients.erase(connect_key);
    }
    _pending_client_cleanup.clear();
}

void WorkerCore::on_deferred_cleanup(evutil_socket_t, short, void *arg)
{
    auto *core = static_cast<WorkerCore *>(arg);
    if (core) {
        core->run_deferred_cleanup();
    }
}

} // namespace navcaster::caster
