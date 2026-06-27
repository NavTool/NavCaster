#include "worker/worker_core.h"

#include <vector>

#include "domain/connect_info.h"
#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {
namespace {

constexpr std::size_t kMaxClientOutputBufferBytes = 1024 * 1024;

} // namespace

WorkerCore::WorkerCore(std::uint32_t worker_id, event_base *base)
    : worker_id_(worker_id), base_(base)
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
    snapshot.slow_client_disconnect_count = slow_client_disconnect_count_;
    snapshot.output_buffer_limit_count = output_buffer_limit_count_;
    snapshot.redis_contexts_reserved = 2;
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
    std::string initial_bytes = std::move(message.initial_bytes);
    message.initial_bytes.clear();

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
    sources_[session_id] = std::move(session);
    if (!initial_bytes.empty()) {
        handle_source_data_locked(session_id, initial_bytes);
    }
}

void WorkerCore::create_client_locked(HandoffMessage message)
{
    const std::string mount = message.connect_info.mount;
    const std::uint64_t session_id = next_session_id_++;
    auto session = std::make_unique<ClientSession>(
        session_id,
        worker_id_,
        std::move(message),
        [this](std::uint64_t client_id) {
            handle_client_closed(client_id);
        });

    if (!session->start(base_)) {
        log_warn("worker " + std::to_string(worker_id_) + " failed to start client session mount=" + mount);
        return;
    }

    mounts_[mount].client_ids.insert(session_id);
    clients_[session_id] = std::move(session);
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

void WorkerCore::handle_source_data_locked(std::uint64_t session_id, const std::string &data)
{
    const auto source_it = sources_.find(session_id);
    if (source_it == sources_.end() || data.empty()) {
        return;
    }

    const std::string mount = source_it->second->mount();
    bytes_in_ += static_cast<std::uint64_t>(data.size());

    auto mount_it = mounts_.find(mount);
    if (mount_it == mounts_.end()) {
        ++redis_publish_count_;
        redis_publish_bytes_ += static_cast<std::uint64_t>(data.size());
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
        bytes_out_ += static_cast<std::uint64_t>(data.size());
        ++fanout_write_count_;
    }

    ++redis_publish_count_;
    redis_publish_bytes_ += static_cast<std::uint64_t>(data.size());
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
    }
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
