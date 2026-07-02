#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "runtime/mount_owner_registry.h"
#include "runtime/runtime_config.h"
#include "transport/handoff_message.h"
#include "worker/caster_worker.h"
#include "worker/worker_metrics.h"

namespace navcaster::caster {

class WorkerManager {
public:
    explicit WorkerManager(RuntimeConfig config);
    ~WorkerManager();

    WorkerManager(const WorkerManager &) = delete;
    WorkerManager &operator=(const WorkerManager &) = delete;

    bool start();
    void stop();
    bool running() const { return running_; }

    bool dispatch_handoff(HandoffMessage message);
    bool post_probe_to_all();
    void set_worker_draining(std::uint32_t worker_id, bool draining);

    std::vector<WorkerMetricsSnapshot> metrics_snapshot() const;
    MountOwnerRegistry &mount_owners() { return mount_registry_; }
    const MountOwnerRegistry &mount_owners() const { return mount_registry_; }

private:
    CasterWorker *find_worker(std::uint32_t worker_id) const;

    RuntimeConfig config_;
    std::vector<std::unique_ptr<CasterWorker>> workers_;
    MountOwnerRegistry mount_registry_;
    bool running_ = false;
};

} // namespace navcaster::caster
