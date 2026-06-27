#include "runtime/worker_manager.h"

#include <utility>

#include "infra/logger.h"

namespace navcaster::caster {

WorkerManager::WorkerManager(RuntimeConfig config)
    : config_(std::move(config))
{
}

WorkerManager::~WorkerManager()
{
    stop();
}

bool WorkerManager::start()
{
    if (running_) {
        return true;
    }

    workers_.reserve(config_.worker_count);
    for (std::uint32_t index = 0; index < config_.worker_count; ++index) {
        const std::uint32_t worker_id = index + 1;
        auto worker = std::make_unique<CasterWorker>(worker_id, config_.redis);
        if (!worker->start()) {
            log_error("failed to start worker " + std::to_string(worker_id));
            stop();
            return false;
        }
        workers_.push_back(std::move(worker));
    }

    running_ = true;
    return true;
}

void WorkerManager::stop()
{
    for (auto &worker : workers_) {
        if (worker) {
            worker->stop();
        }
    }
    workers_.clear();
    running_ = false;
}

bool WorkerManager::dispatch_handoff(HandoffMessage message)
{
    if (!running_) {
        return false;
    }

    const auto snapshots = metrics_snapshot();
    const auto worker_id = mount_registry_.resolve_or_assign(message.connect_info.mount, snapshots);
    CasterWorker *worker = find_worker(worker_id);
    if (!worker) {
        return false;
    }

    return worker->post_handoff(std::move(message));
}

bool WorkerManager::post_probe_to_all()
{
    bool ok = running_;
    for (auto &worker : workers_) {
        if (!worker || !worker->post_probe()) {
            ok = false;
        }
    }
    return ok;
}

void WorkerManager::set_worker_draining(std::uint32_t worker_id, bool draining)
{
    mount_registry_.set_worker_draining(worker_id, draining);
    if (auto *worker = find_worker(worker_id)) {
        worker->set_draining(draining);
    }
}

std::vector<WorkerMetricsSnapshot> WorkerManager::metrics_snapshot() const
{
    std::vector<WorkerMetricsSnapshot> snapshots;
    snapshots.reserve(workers_.size());
    for (const auto &worker : workers_) {
        if (worker) {
            snapshots.push_back(worker->snapshot());
        }
    }
    return snapshots;
}

CasterWorker *WorkerManager::find_worker(std::uint32_t worker_id) const
{
    for (const auto &worker : workers_) {
        if (worker && worker->id() == worker_id) {
            return worker.get();
        }
    }
    return nullptr;
}

} // namespace navcaster::caster
