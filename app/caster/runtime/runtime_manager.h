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
    bool running() const { return _running.load(); }

    RuntimeMetricsSnapshot metrics_snapshot() const;
    RuntimeSelfTestResult run_self_test();

private:
    void runtime_loop();
    bool dispatch_handoff(HandoffMessage message);
    bool respond_source_table(HandoffMessage message);

    RuntimeConfig _config;
    std::unique_ptr<WorkerManager> _worker_manager;
    std::unique_ptr<RuntimeHealthServer> _health_server;
    std::unique_ptr<Acceptor> _acceptor;
    EventBasePtr _base;
    std::thread _runtime_thread;
    std::chrono::steady_clock::time_point _started_at{};
    std::atomic<bool> _running{false};
};

} // namespace navcaster::caster
