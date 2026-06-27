#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "worker/worker_metrics.h"

namespace navcaster::caster {

struct RuntimeMetricsSnapshot {
    std::string runtime_id;
    bool running = false;
    std::uint64_t uptime_ms = 0;
    std::uint32_t worker_count = 0;
    std::uint64_t mount_count = 0;
    std::vector<WorkerMetricsSnapshot> workers;
};

std::string runtime_metrics_to_json(const RuntimeMetricsSnapshot &snapshot);

} // namespace navcaster::caster
