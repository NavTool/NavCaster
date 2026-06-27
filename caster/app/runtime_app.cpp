#include "app/runtime_app.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "infra/event_loop.h"
#include "infra/logger.h"
#include "infra/socket_util.h"
#include "runtime/runtime_manager.h"

namespace navcaster::caster {
namespace {

std::atomic<bool> g_shutdown_requested{false};

void handle_signal(int)
{
    g_shutdown_requested.store(true);
}

} // namespace

int run_runtime_app(const RuntimeConfig &config)
{
    SocketRuntimeGuard socket_guard;
    if (!socket_guard.ok()) {
        log_error("failed to initialize socket runtime");
        return 2;
    }

    EventThreadingGuard event_threading_guard;
    if (!event_threading_guard.ok()) {
        log_error("failed to initialize libevent thread support");
        return 2;
    }

    RuntimeManager runtime(config);
    if (config.self_test) {
        const auto result = runtime.run_self_test();
        std::cout << result.report_json << std::endl;
        return result.ok ? 0 : 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (!runtime.start()) {
        log_error("failed to start navcaster-caster runtime");
        return 2;
    }

    log_info("navcaster-caster runtime started");
    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    runtime.stop();
    log_info("navcaster-caster runtime stopped");
    return 0;
}

} // namespace navcaster::caster
