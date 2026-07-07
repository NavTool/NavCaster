#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "worker/worker_metrics.h"

namespace navcaster::caster {

struct MountOwnerSnapshot {
    std::string mount;
    std::uint32_t worker_id = 0;
    bool draining = false;
};

class MountOwnerRegistry {
public:
    bool assign_mount(const std::string &mount, std::uint32_t worker_id);
    std::uint32_t owner_for_mount(const std::string &mount) const;
    std::uint32_t resolve_or_assign(const std::string &mount, const std::vector<WorkerMetricsSnapshot> &workers);
    void release_mount(const std::string &mount);
    void set_worker_draining(std::uint32_t worker_id, bool draining);
    bool worker_draining(std::uint32_t worker_id) const;
    std::vector<MountOwnerSnapshot> snapshot() const;
    std::uint64_t mount_count() const;

private:
    std::uint32_t choose_worker(const std::vector<WorkerMetricsSnapshot> &workers) const;

    mutable std::mutex _mutex;
    std::unordered_map<std::string, std::uint32_t> _mount_to_worker;
    std::unordered_set<std::uint32_t> _draining_workers;
};

} // namespace navcaster::caster
