#include "worker/worker_core.h"

#include "infra/socket_util.h"

namespace navcaster::caster {

WorkerCore::WorkerCore(std::uint32_t worker_id)
    : worker_id_(worker_id)
{
}

void WorkerCore::accept_handoff(HandoffMessage message)
{
    ++handoff_received_;
    if (!message.connect_info.mount.empty()) {
        active_mounts_ = active_mounts_ == 0 ? 1 : active_mounts_;
    }
    if (message.fd >= 0) {
        ++active_sessions_;
        close_socket(message.fd);
        --active_sessions_;
    }
}

WorkerMetricsSnapshot WorkerCore::snapshot() const
{
    WorkerMetricsSnapshot snapshot;
    snapshot.worker_id = worker_id_;
    snapshot.draining = draining_;
    snapshot.handoff_received = handoff_received_;
    snapshot.active_sessions = active_sessions_;
    snapshot.active_mounts = active_mounts_;
    snapshot.redis_contexts_reserved = 2;
    return snapshot;
}

void WorkerCore::set_draining(bool draining)
{
    draining_ = draining;
}

} // namespace navcaster::caster
