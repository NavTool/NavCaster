#include "worker/caster_worker.h"

#include <chrono>
#include <sstream>
#include <utility>

#include <event2/event.h>

#include "infra/logger.h"

namespace navcaster::caster {

CasterWorker::CasterWorker(std::uint32_t worker_id, std::string runtime_id, RedisEndpoint redis)
    : worker_id_(worker_id), runtime_id_(std::move(runtime_id)), redis_endpoint_(std::move(redis))
{
}

CasterWorker::~CasterWorker()
{
    stop();
}

bool CasterWorker::start()
{
    if (running_.load()) {
        return true;
    }

    stop_requested_.store(false);
    running_.store(true);
    thread_ = std::thread(&CasterWorker::thread_main, this);
    if (wait_until_ready()) {
        return true;
    }

    stop();
    return false;
}

void CasterWorker::stop()
{
    if (!running_.load() && !thread_.joinable()) {
        return;
    }

    stop_requested_.store(true);
    if (base_) {
        event_base_loopbreak(base_.get());
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool CasterWorker::post_handoff(HandoffMessage message)
{
    if (!ready_.load() || !mailbox_) {
        return false;
    }

    return mailbox_->post([this, message = std::move(message)]() mutable {
        if (core_) {
            core_->accept_handoff(std::move(message));
        }
    });
}

bool CasterWorker::post_probe()
{
    if (!ready_.load() || !mailbox_) {
        return false;
    }

    return mailbox_->post([]() {});
}

void CasterWorker::set_draining(bool draining)
{
    if (!ready_.load() || !mailbox_) {
        return;
    }

    mailbox_->post([this, draining]() {
        if (core_) {
            core_->set_draining(draining);
        }
    });
}

WorkerMetricsSnapshot CasterWorker::snapshot() const
{
    WorkerMetricsSnapshot snapshot;
    if (core_) {
        snapshot = core_->snapshot();
    } else {
        snapshot.worker_id = worker_id_;
    }
    snapshot.running = running_.load();
    if (mailbox_) {
        snapshot.mailbox_messages = mailbox_->posted_count();
    }
    return snapshot;
}

void CasterWorker::thread_main()
{
    base_ = make_event_base();
    if (!base_) {
        log_error("worker " + std::to_string(worker_id_) + " failed to create event_base");
        running_.store(false);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            ready_.store(false);
        }
        ready_cv_.notify_all();
        return;
    }

    redis_boundary_ = std::make_unique<WorkerRedisBoundary>(runtime_id_, worker_id_, redis_endpoint_.host, redis_endpoint_.port);
    core_ = std::make_unique<WorkerCore>(worker_id_, base_.get(), redis_boundary_.get());
    mailbox_ = std::make_unique<WorkerMailbox>();
    if (!mailbox_->attach(base_.get())) {
        log_error("worker " + std::to_string(worker_id_) + " failed to attach mailbox");
        running_.store(false);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            ready_.store(false);
        }
        ready_cv_.notify_all();
        return;
    }

    redis_boundary_->start(
        base_.get(),
        [this](std::string origin_runtime_id, std::string mount, std::string payload) {
            if (core_) {
                core_->handle_redis_mount_data(std::move(origin_runtime_id), std::move(mount), std::move(payload));
            }
        },
        [this](std::string operation) {
            if (core_) {
                core_->handle_redis_error(operation);
            }
        });

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        ready_.store(true);
    }
    ready_cv_.notify_all();

    event_base_loop(base_.get(), EVLOOP_NO_EXIT_ON_EMPTY);

    if (mailbox_) {
        mailbox_->detach();
    }
    if (redis_boundary_) {
        redis_boundary_->stop();
    }
    mailbox_.reset();
    core_.reset();
    redis_boundary_.reset();
    base_.reset();
    ready_.store(false);
    running_.store(false);
}

bool CasterWorker::wait_until_ready()
{
    std::unique_lock<std::mutex> lock(state_mutex_);
    return ready_cv_.wait_for(lock, std::chrono::seconds(5), [this]() {
        return ready_.load() || !running_.load();
    }) && ready_.load();
}

} // namespace navcaster::caster
