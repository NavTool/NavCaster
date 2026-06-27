#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "infra/event_loop.h"
#include "runtime/runtime_config.h"
#include "runtime/runtime_health_server.h"
#include "runtime/runtime_metrics.h"
#include "runtime/worker_manager.h"
#include "transport/acceptor.h"

namespace navcaster::caster {

struct RuntimeSelfTestResult {
    bool ok = false;
    std::string report_json;
};

class RuntimeManager {
public:
    explicit RuntimeManager(RuntimeConfig config);
    ~RuntimeManager();

    RuntimeManager(const RuntimeManager &) = delete;
    RuntimeManager &operator=(const RuntimeManager &) = delete;

    bool start();
    void stop();
    bool running() const { return running_.load(); }

    RuntimeMetricsSnapshot metrics_snapshot() const;
    RuntimeSelfTestResult run_self_test();

private:
    void runtime_loop();
    bool dispatch_handoff(HandoffMessage message);

    RuntimeConfig config_;
    std::unique_ptr<WorkerManager> worker_manager_;
    std::unique_ptr<RuntimeHealthServer> health_server_;
    std::unique_ptr<Acceptor> acceptor_;
    EventBasePtr base_;
    std::thread runtime_thread_;
    std::chrono::steady_clock::time_point started_at_{};
    std::atomic<bool> running_{false};
};

} // namespace navcaster::caster
