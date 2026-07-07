#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "domain/connect_info.h"
#include "infra/event_loop.h"
#include "runtime/runtime_config.h"
#include "runtime/runtime_health_server.h"
#include "runtime/runtime_metrics.h"
#include "runtime/worker_manager.h"
#include "session/source_session.h"
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
    bool create_source_session(HandoffMessage message);
    std::string source_table_body(const ConnectInfo &info) const;
    void handle_source_closed(std::uint64_t session_id);
    void schedule_source_cleanup();
    void run_source_cleanup();

    static void on_source_cleanup(evutil_socket_t fd, short what, void *arg);

    RuntimeConfig _config;
    std::unique_ptr<WorkerManager> _worker_manager;
    std::unique_ptr<RuntimeHealthServer> _health_server;
    std::unique_ptr<Acceptor> _acceptor;
    std::unordered_map<std::uint64_t, std::unique_ptr<SourceSession>> _source_sessions;
    std::unordered_set<std::uint64_t> _pending_source_cleanup;
    std::uint64_t _next_source_session_id = 1;
    bool _source_cleanup_scheduled = false;
    EventBasePtr _base;
    std::thread _runtime_thread;
    std::chrono::steady_clock::time_point _started_at{};
    std::atomic<bool> _running{false};
};

} // namespace navcaster::caster
