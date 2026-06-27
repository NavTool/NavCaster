#pragma once

#include <cstdint>
#include <unordered_map>

#include "transport/handoff_message.h"
#include "worker/worker_metrics.h"

namespace navcaster::caster {

class WorkerCore {
public:
    explicit WorkerCore(std::uint32_t worker_id);

    void accept_handoff(HandoffMessage message);
    WorkerMetricsSnapshot snapshot() const;
    void set_draining(bool draining);

private:
    std::uint32_t worker_id_ = 0;
    bool draining_ = false;
    std::uint64_t handoff_received_ = 0;
    std::uint64_t active_sessions_ = 0;
    std::uint64_t active_mounts_ = 0;
};

} // namespace navcaster::caster
