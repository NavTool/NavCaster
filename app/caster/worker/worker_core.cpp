#include "worker/worker_core.h"

#include <algorithm>
#include <vector>

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
    : worker_id_(worker_id), base_(base), redis_boundary_(redis_boundary)
{
}

void WorkerCore::accept_handoff(HandoffMessage message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++handoff_received_;

    if (!base_ || message.fd < 0 || message.connect_info.mount.empty() || message.connect_info.type == ConnectType::Unknown) {
        reject_handoff_locked(message, "invalid_handoff");
        return;
    }

    switch (message.connect_info.type) {
    case ConnectType::Source:
        create_source_locked(std::move(message));
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
    std::lock_guard<std::mutex> lock(mutex_);
    WorkerMetricsSnapshot snapshot;
    snapshot.worker_id = worker_id_;
    snapshot.draining = draining_;
    snapshot.handoff_received = handoff_received_;
    snapshot.source_count = static_cast<std::uint64_t>(sources_.size());
    snapshot.client_count = static_cast<std::uint64_t>(clients_.size());
    snapshot.active_sessions = snapshot.source_count + snapshot.client_count;
    snapshot.active_mounts = static_cast<std::uint64_t>(mounts_.size());
    snapshot.bytes_in = bytes_in_;
    snapshot.bytes_out = bytes_out_;
    snapshot.fanout_write_count = fanout_write_count_;
    snapshot.redis_publish_count = redis_publish_count_;
    snapshot.redis_publish_bytes = redis_publish_bytes_;
    snapshot.redis_publish_error_count = redis_publish_error_count_;
    snapshot.redis_subscribe_message_count = redis_subscribe_message_count_;
    snapshot.redis_subscribe_bytes = redis_subscribe_bytes_;
    snapshot.redis_remote_fanout_write_count = redis_remote_fanout_write_count_;
    snapshot.redis_remote_fanout_bytes = redis_remote_fanout_bytes_;
    snapshot.redis_error_count = redis_error_count_;
    snapshot.redis_subscribed_mount_count = redis_subscribed_mount_count_;
    snapshot.redis_position_report_count = redis_position_report_count_;
    snapshot.redis_position_report_error_count = redis_position_report_error_count_;
    snapshot.slow_client_disconnect_count = slow_client_disconnect_count_;
    snapshot.output_buffer_limit_count = output_buffer_limit_count_;
    snapshot.redis_connected = redis_boundary_ && redis_boundary_->connected();
    snapshot.redis_contexts_reserved = 2;
    snapshot.mounts.reserve(mounts_.size());
    for (const auto &item : mounts_) {
        const auto &state = item.second;
        MountMetricsSnapshot mount;
        mount.worker_id = worker_id_;
        mount.mount = item.first;
        mount.source_online = state.source_id != 0;
        mount.client_count = static_cast<std::uint64_t>(state.client_ids.size());
        mount.source_bytes_in = state.source_bytes_in;
        mount.source_rtcm_frame_count = state.source_rtcm_frame_count;
        mount.base_position_report_count = state.base_position_report_count;
        mount.base_position_source = state.base_position_source;
        mount.base_position = state.base_position;
        snapshot.mounts.push_back(std::move(mount));
    }
    snapshot.clients.reserve(client_states_.size());
    for (const auto &item : client_states_) {
        const auto &state = item.second;
        ClientMetricsSnapshot client;
        client.worker_id = worker_id_;
        client.session_id = item.first;
        client.member_key = state.member_key;
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
    std::lock_guard<std::mutex> lock(mutex_);
    draining_ = draining;
}

void WorkerCore::create_source_locked(HandoffMessage message)
{
    const std::string mount = message.connect_info.mount;

    const auto existing_source_id = mounts_[mount].source_id;
    if (existing_source_id != 0) {
        close_source_locked(existing_source_id);
    }

    const std::uint64_t session_id = next_session_id_++;
    auto session = std::make_unique<SourceSession>(
        session_id,
        worker_id_,
        std::move(message),
        [this](std::uint64_t source_id, std::string data) {
            handle_source_data(source_id, std::move(data));
        },
        [this](std::uint64_t source_id) {
            handle_source_closed(source_id);
        });

    if (!session->start(base_)) {
        log_warn("worker " + std::to_string(worker_id_) + " failed to start source session mount=" + mount);
        erase_mount_if_empty_locked(mount);
        return;
    }

    auto &mount_state = mounts_[mount];
    mount_state.source_id = session_id;
    source_decoders_[session_id] = Rtcm3Parser{};
    sources_[session_id] = std::move(session);
    const auto initial_payload = sources_[session_id]->consume_initial_bytes();
    if (!initial_payload.empty()) {
        handle_source_data_locked(session_id, initial_payload);
    }
    if (sources_.find(session_id) != sources_.end() &&
        (sources_[session_id]->input_failed() || sources_[session_id]->input_complete())) {
        close_source_locked(session_id);
    }
}

void WorkerCore::create_client_locked(HandoffMessage message)
{
    const std::string mount = message.connect_info.mount;
    const std::string initial_gga = message.connect_info.initial_gga;
    const std::uint64_t session_id = next_session_id_++;
    auto session = std::make_unique<ClientSession>(
        session_id,
        worker_id_,
        std::move(message),
        [this](std::uint64_t client_id, std::string data) {
            handle_client_data(client_id, std::move(data));
        },
        [this](std::uint64_t client_id) {
            handle_client_closed(client_id);
        });

    if (!session->start(base_)) {
        log_warn("worker " + std::to_string(worker_id_) + " failed to start client session mount=" + mount);
        return;
    }

    auto &mount_state = mounts_[mount];
    const bool should_subscribe = mount_state.client_ids.empty();
    mount_state.client_ids.insert(session_id);
    ClientRuntimeState client_state;
    client_state.mount = mount;
    client_state.remote_addr = session->remote_addr();
    client_state.remote_port = session->remote_port();
    client_state.member_key = client_member_key(session_id);
    client_states_[session_id] = std::move(client_state);
    clients_[session_id] = std::move(session);
    if (!initial_gga.empty()) {
        handle_client_data_locked(session_id, initial_gga);
    }
    if (clients_.find(session_id) != clients_.end()) {
        const auto initial_payload = clients_[session_id]->consume_initial_bytes();
        if (!initial_payload.empty()) {
            handle_client_data_locked(session_id, initial_payload);
        }
    }
    if (clients_.find(session_id) != clients_.end() &&
        (clients_[session_id]->input_failed() || clients_[session_id]->input_complete())) {
        close_client_locked(session_id);
    }
    if (clients_.find(session_id) == clients_.end()) {
        return;
    }
    if (redis_boundary_ && should_subscribe) {
        if (redis_boundary_->subscribe_mount(mount)) {
            ++redis_subscribed_mount_count_;
        } else {
            ++redis_error_count_;
        }
    }
}

void WorkerCore::reject_handoff_locked(HandoffMessage &message, const std::string &reason)
{
    if (message.fd >= 0) {
        close_socket(message.fd);
        message.fd = -1;
    }
    log_warn("worker " + std::to_string(worker_id_) + " rejected handoff reason=" + reason +
             " type=" + connect_type_name(message.connect_info.type) + " mount=" + message.connect_info.mount);
}

void WorkerCore::handle_source_data(std::uint64_t session_id, std::string data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    handle_source_data_locked(session_id, data);
}

void WorkerCore::handle_client_data(std::uint64_t session_id, std::string data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    handle_client_data_locked(session_id, data);
}

void WorkerCore::handle_source_closed(std::uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    detach_source_locked(session_id);
    pending_source_cleanup_.insert(session_id);
    schedule_deferred_cleanup_locked();
}

void WorkerCore::handle_client_closed(std::uint64_t session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    detach_client_locked(session_id);
    pending_client_cleanup_.insert(session_id);
    schedule_deferred_cleanup_locked();
}

void WorkerCore::handle_redis_mount_data(std::string origin_runtime_id, std::string mount, std::string data)
{
    (void)origin_runtime_id;
    std::lock_guard<std::mutex> lock(mutex_);
    if (mount.empty() || data.empty()) {
        return;
    }
    ++redis_subscribe_message_count_;
    redis_subscribe_bytes_ += static_cast<std::uint64_t>(data.size());

    std::uint64_t writes = 0;
    std::uint64_t bytes = 0;
    fanout_to_mount_clients_locked(mount, data, true, &writes, &bytes);
    bytes_out_ += bytes;
    redis_remote_fanout_write_count_ += writes;
    redis_remote_fanout_bytes_ += bytes;
}

void WorkerCore::handle_redis_error(const std::string &operation)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++redis_error_count_;
    if (operation == "publish") {
        ++redis_publish_error_count_;
    }
}

void WorkerCore::handle_source_data_locked(std::uint64_t session_id, const std::string &data)
{
    const auto source_it = sources_.find(session_id);
    if (source_it == sources_.end() || data.empty()) {
        return;
    }

    const std::string mount = source_it->second->mount();
    bytes_in_ += static_cast<std::uint64_t>(data.size());
    auto &mount_state = mounts_[mount];
    mount_state.source_bytes_in += static_cast<std::uint64_t>(data.size());

    auto decoder_it = source_decoders_.find(session_id);
    if (decoder_it != source_decoders_.end()) {
        const auto reports = decoder_it->second.feed(data);
        mount_state.source_rtcm_frame_count += static_cast<std::uint64_t>(reports.size());
        for (const auto &report : reports) {
            update_mount_position_locked(mount, report);
        }
    }

    std::uint64_t writes = 0;
    std::uint64_t bytes = 0;
    fanout_to_mount_clients_locked(mount, data, false, &writes, &bytes);
    fanout_write_count_ += writes;
    bytes_out_ += bytes;

    ++redis_publish_count_;
    redis_publish_bytes_ += static_cast<std::uint64_t>(data.size());
    if (redis_boundary_ && !redis_boundary_->publish_mount_data(mount, data)) {
        ++redis_publish_error_count_;
        ++redis_error_count_;
    }
}

void WorkerCore::handle_client_data_locked(std::uint64_t session_id, const std::string &data)
{
    if (data.empty()) {
        return;
    }
    auto state_it = client_states_.find(session_id);
    if (state_it == client_states_.end()) {
        return;
    }

    auto &state = state_it->second;
    state.nmea_buffer += data;
    if (state.nmea_buffer.size() > 8192) {
        state.nmea_buffer.erase(0, state.nmea_buffer.size() - 8192);
    }

    if (auto report = parse_latest_nmea_gga(state.nmea_buffer)) {
        update_client_position_locked(session_id, *report);
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
    auto &state = mounts_[mount];
    state.base_position = report.position;
    state.base_position.updated_at_ms = steady_time_ms();
    state.base_position_source = report.source;
    ++state.base_position_report_count;

    if (redis_boundary_) {
        if (redis_boundary_->report_mount_position(mount, state.base_position)) {
            ++redis_position_report_count_;
        } else {
            ++redis_position_report_error_count_;
            ++redis_error_count_;
        }
    }
}

void WorkerCore::update_client_position_locked(std::uint64_t session_id, const PositionReport &report)
{
    if (!report.position.valid) {
        return;
    }
    auto state_it = client_states_.find(session_id);
    if (state_it == client_states_.end()) {
        return;
    }

    auto &state = state_it->second;
    state.position = report.position;
    state.position.updated_at_ms = steady_time_ms();
    state.position_source = report.source;
    ++state.position_report_count;

    if (redis_boundary_) {
        if (redis_boundary_->report_client_position(state.member_key, state.position)) {
            ++redis_position_report_count_;
        } else {
            ++redis_position_report_error_count_;
            ++redis_error_count_;
        }
    }
}

std::string WorkerCore::client_member_key(std::uint64_t session_id) const
{
    std::string prefix = redis_boundary_ ? redis_boundary_->runtime_id() : std::string("local-runtime");
    return prefix + ":worker-" + std::to_string(worker_id_) + ":client-" + std::to_string(session_id);
}

void WorkerCore::fanout_to_mount_clients_locked(
    const std::string &mount,
    const std::string &data,
    bool remote,
    std::uint64_t *write_count,
    std::uint64_t *write_bytes)
{
    auto mount_it = mounts_.find(mount);
    if (mount_it == mounts_.end()) {
        return;
    }
    std::vector<std::uint64_t> client_ids;
    client_ids.reserve(mount_it->second.client_ids.size());
    for (const auto client_id : mount_it->second.client_ids) {
        client_ids.push_back(client_id);
    }

    for (const auto client_id : client_ids) {
        auto client_it = clients_.find(client_id);
        if (client_it == clients_.end()) {
            continue;
        }
        ClientSession *client = client_it->second.get();
        if (client->mount() != mount) {
            continue;
        }
        if (client->pending_output_bytes() + data.size() > kMaxClientOutputBufferBytes) {
            ++slow_client_disconnect_count_;
            ++output_buffer_limit_count_;
            close_client_locked(client_id);
            continue;
        }
        if (!client->write_bytes(data.data(), data.size())) {
            close_client_locked(client_id);
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

void WorkerCore::close_source_locked(std::uint64_t session_id)
{
    auto source_it = sources_.find(session_id);
    if (source_it == sources_.end()) {
        return;
    }

    detach_source_locked(session_id);
    sources_.erase(source_it);
}

void WorkerCore::close_client_locked(std::uint64_t session_id)
{
    auto client_it = clients_.find(session_id);
    if (client_it == clients_.end()) {
        return;
    }

    detach_client_locked(session_id);
    clients_.erase(client_it);
}

void WorkerCore::erase_mount_if_empty_locked(const std::string &mount)
{
    auto mount_it = mounts_.find(mount);
    if (mount_it == mounts_.end()) {
        return;
    }
    if (mount_it->second.source_id == 0 && mount_it->second.client_ids.empty()) {
        mounts_.erase(mount_it);
    }
}

void WorkerCore::detach_source_locked(std::uint64_t session_id)
{
    auto source_it = sources_.find(session_id);
    if (source_it == sources_.end()) {
        return;
    }

    const std::string mount = source_it->second->mount();
    auto mount_it = mounts_.find(mount);
    if (mount_it != mounts_.end() && mount_it->second.source_id == session_id) {
        mount_it->second.source_id = 0;
    }
    source_decoders_.erase(session_id);
    erase_mount_if_empty_locked(mount);
}

void WorkerCore::detach_client_locked(std::uint64_t session_id)
{
    auto client_it = clients_.find(session_id);
    if (client_it == clients_.end()) {
        return;
    }

    const std::string mount = client_it->second->mount();
    auto mount_it = mounts_.find(mount);
    if (mount_it != mounts_.end()) {
        mount_it->second.client_ids.erase(session_id);
        if (mount_it->second.client_ids.empty() && redis_boundary_) {
            if (!redis_boundary_->unsubscribe_mount(mount)) {
                ++redis_error_count_;
            }
            if (redis_subscribed_mount_count_ > 0) {
                --redis_subscribed_mount_count_;
            }
        }
    }
    client_states_.erase(session_id);
    erase_mount_if_empty_locked(mount);
}

void WorkerCore::schedule_deferred_cleanup_locked()
{
    if (cleanup_scheduled_ || !base_) {
        return;
    }

    cleanup_scheduled_ = event_base_once(base_, -1, EV_TIMEOUT, &WorkerCore::on_deferred_cleanup, this, nullptr) == 0;
}

void WorkerCore::run_deferred_cleanup()
{
    std::lock_guard<std::mutex> lock(mutex_);
    cleanup_scheduled_ = false;
    for (const auto session_id : pending_source_cleanup_) {
        sources_.erase(session_id);
    }
    pending_source_cleanup_.clear();
    for (const auto session_id : pending_client_cleanup_) {
        clients_.erase(session_id);
    }
    pending_client_cleanup_.clear();
}

void WorkerCore::on_deferred_cleanup(evutil_socket_t, short, void *arg)
{
    auto *core = static_cast<WorkerCore *>(arg);
    if (core) {
        core->run_deferred_cleanup();
    }
}

} // namespace navcaster::caster
