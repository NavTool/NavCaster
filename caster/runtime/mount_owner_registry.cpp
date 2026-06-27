#include "runtime/mount_owner_registry.h"

#include <limits>

namespace navcaster::caster {

bool MountOwnerRegistry::assign_mount(const std::string &mount, std::uint32_t worker_id)
{
    if (mount.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (draining_workers_.find(worker_id) != draining_workers_.end()) {
        return false;
    }
    mount_to_worker_[mount] = worker_id;
    return true;
}

std::uint32_t MountOwnerRegistry::owner_for_mount(const std::string &mount) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = mount_to_worker_.find(mount);
    return it == mount_to_worker_.end() ? 0 : it->second;
}

std::uint32_t MountOwnerRegistry::resolve_or_assign(const std::string &mount, const std::vector<WorkerMetricsSnapshot> &workers)
{
    if (mount.empty()) {
        return choose_worker(workers);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto existing = mount_to_worker_.find(mount);
        if (existing != mount_to_worker_.end()) {
            return existing->second;
        }
    }

    const auto worker_id = choose_worker(workers);
    if (worker_id == 0) {
        return 0;
    }
    assign_mount(mount, worker_id);
    return worker_id;
}

void MountOwnerRegistry::release_mount(const std::string &mount)
{
    std::lock_guard<std::mutex> lock(mutex_);
    mount_to_worker_.erase(mount);
}

void MountOwnerRegistry::set_worker_draining(std::uint32_t worker_id, bool draining)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (draining) {
        draining_workers_.insert(worker_id);
    } else {
        draining_workers_.erase(worker_id);
    }
}

bool MountOwnerRegistry::worker_draining(std::uint32_t worker_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return draining_workers_.find(worker_id) != draining_workers_.end();
}

std::vector<MountOwnerSnapshot> MountOwnerRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MountOwnerSnapshot> snapshots;
    snapshots.reserve(mount_to_worker_.size());
    for (const auto &entry : mount_to_worker_) {
        MountOwnerSnapshot snapshot;
        snapshot.mount = entry.first;
        snapshot.worker_id = entry.second;
        snapshot.draining = draining_workers_.find(entry.second) != draining_workers_.end();
        snapshots.push_back(snapshot);
    }
    return snapshots;
}

std::uint64_t MountOwnerRegistry::mount_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return mount_to_worker_.size();
}

std::uint32_t MountOwnerRegistry::choose_worker(const std::vector<WorkerMetricsSnapshot> &workers) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint32_t chosen = 0;
    std::uint64_t best_score = std::numeric_limits<std::uint64_t>::max();
    for (const auto &worker : workers) {
        if (!worker.running || worker.draining) {
            continue;
        }
        if (draining_workers_.find(worker.worker_id) != draining_workers_.end()) {
            continue;
        }

        const auto score = worker.active_sessions + worker.active_mounts * 10;
        if (score < best_score || (score == best_score && worker.worker_id < chosen)) {
            best_score = score;
            chosen = worker.worker_id;
        }
    }
    return chosen;
}

} // namespace navcaster::caster
