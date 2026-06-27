#include "runtime/runtime_manager.h"

#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

#include <event2/event.h>

#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {

RuntimeManager::RuntimeManager(RuntimeConfig config)
    : config_(std::move(config))
{
}

RuntimeManager::~RuntimeManager()
{
    stop();
}

bool RuntimeManager::start()
{
    if (running_.load()) {
        return true;
    }

    base_ = make_event_base();
    if (!base_) {
        log_error("runtime manager failed to create control event_base");
        return false;
    }

    worker_manager_ = std::make_unique<WorkerManager>(config_);
    if (!worker_manager_->start()) {
        stop();
        return false;
    }

    if (config_.enable_health_api) {
        health_server_ = std::make_unique<RuntimeHealthServer>(config_, [this]() {
            return metrics_snapshot();
        });
        if (!health_server_->start(base_.get())) {
            stop();
            return false;
        }
    }

    if (config_.enable_acceptor) {
        acceptor_ = std::make_unique<Acceptor>(config_, [this](HandoffMessage message) {
            return dispatch_handoff(std::move(message));
        });
        if (!acceptor_->start(base_.get())) {
            stop();
            return false;
        }
    }

    started_at_ = std::chrono::steady_clock::now();
    running_.store(true);
    runtime_thread_ = std::thread(&RuntimeManager::runtime_loop, this);
    return true;
}

void RuntimeManager::stop()
{
    running_.store(false);
    if (base_) {
        event_base_loopbreak(base_.get());
    }
    if (runtime_thread_.joinable()) {
        runtime_thread_.join();
    }

    acceptor_.reset();
    health_server_.reset();
    if (worker_manager_) {
        worker_manager_->stop();
        worker_manager_.reset();
    }
    base_.reset();
}

RuntimeMetricsSnapshot RuntimeManager::metrics_snapshot() const
{
    RuntimeMetricsSnapshot snapshot;
    snapshot.runtime_id = config_.runtime_id;
    snapshot.running = running_.load();
    if (snapshot.running) {
        const auto elapsed = std::chrono::steady_clock::now() - started_at_;
        snapshot.uptime_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
    if (worker_manager_) {
        snapshot.workers = worker_manager_->metrics_snapshot();
        snapshot.worker_count = static_cast<std::uint32_t>(snapshot.workers.size());
        snapshot.mount_count = worker_manager_->mount_owners().mount_count();
        snapshot.mount_owners = worker_manager_->mount_owners().snapshot();
    }
    return snapshot;
}

RuntimeSelfTestResult RuntimeManager::run_self_test()
{
    RuntimeSelfTestResult result;
    if (!start()) {
        result.report_json = "{\"ok\":false,\"error\":\"start_failed\"}";
        return result;
    }

    bool handoff_ok = false;
    if (worker_manager_) {
        const auto owner_id = worker_manager_->mount_owners().resolve_or_assign("SELFTEST", worker_manager_->metrics_snapshot());
        handoff_ok = owner_id != 0;
    }
    const bool probes_ok = worker_manager_ && worker_manager_->post_probe_to_all();

    if (worker_manager_) {
        worker_manager_->set_worker_draining(1, true);
        worker_manager_->set_worker_draining(1, false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(config_.self_test_duration_ms));
    const auto snapshot = metrics_snapshot();

    bool workers_ok = snapshot.worker_count == config_.worker_count;
    for (const auto &worker : snapshot.workers) {
        workers_ok = workers_ok && worker.running && worker.mailbox_messages > 0;
    }

    result.ok = handoff_ok && probes_ok && workers_ok && snapshot.mount_count == 1;
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"handoff_ok\":" << (handoff_ok ? "true" : "false")
        << ",\"probes_ok\":" << (probes_ok ? "true" : "false")
        << ",\"snapshot\":" << runtime_metrics_to_json(snapshot)
        << "}";
    result.report_json = out.str();

    stop();
    return result;
}

void RuntimeManager::runtime_loop()
{
    if (base_) {
        event_base_dispatch(base_.get());
    }
}

bool RuntimeManager::dispatch_handoff(HandoffMessage message)
{
    if (!worker_manager_) {
        if (message.fd >= 0) {
            close_socket(message.fd);
        }
        return false;
    }

    return worker_manager_->dispatch_handoff(std::move(message));
}

} // namespace navcaster::caster
