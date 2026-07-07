#include "runtime/mount_owner_registry.h"

#include <limits>

namespace navcaster::caster {

bool MountOwnerRegistry::assign_mount(const std::string &mount, std::uint32_t worker_id)
{
    if (mount.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    if (_draining_workers.find(worker_id) != _draining_workers.end()) {
        return false;
    }
    _mount_to_worker[mount] = worker_id;
    return true;
}

std::uint32_t MountOwnerRegistry::owner_for_mount(const std::string &mount) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _mount_to_worker.find(mount);
    return it == _mount_to_worker.end() ? 0 : it->second;
}

std::uint32_t MountOwnerRegistry::resolve_or_assign(const std::string &mount, const std::vector<WorkerMetricsSnapshot> &workers)
{
    if (mount.empty()) {
        return choose_worker(workers);
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        const auto existing = _mount_to_worker.find(mount);
        if (existing != _mount_to_worker.end()) {
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
    std::lock_guard<std::mutex> lock(_mutex);
    _mount_to_worker.erase(mount);
}

void MountOwnerRegistry::set_worker_draining(std::uint32_t worker_id, bool draining)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (draining) {
        _draining_workers.insert(worker_id);
    } else {
        _draining_workers.erase(worker_id);
    }
}

bool MountOwnerRegistry::worker_draining(std::uint32_t worker_id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _draining_workers.find(worker_id) != _draining_workers.end();
}

std::vector<MountOwnerSnapshot> MountOwnerRegistry::snapshot() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<MountOwnerSnapshot> snapshots;
    snapshots.reserve(_mount_to_worker.size());
    for (const auto &entry : _mount_to_worker) {
        MountOwnerSnapshot snapshot;
        snapshot.mount = entry.first;
        snapshot.worker_id = entry.second;
        snapshot.draining = _draining_workers.find(entry.second) != _draining_workers.end();
        snapshots.push_back(snapshot);
    }
    return snapshots;
}

std::uint64_t MountOwnerRegistry::mount_count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _mount_to_worker.size();
}

std::uint32_t MountOwnerRegistry::choose_worker(const std::vector<WorkerMetricsSnapshot> &workers) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::uint32_t chosen = 0;
    std::uint64_t best_score = std::numeric_limits<std::uint64_t>::max();
    for (const auto &worker : workers) {
        if (!worker.running || worker.draining) {
            continue;
        }
        if (_draining_workers.find(worker.worker_id) != _draining_workers.end()) {
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
