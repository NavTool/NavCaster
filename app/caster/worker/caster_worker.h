#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "infra/event_loop.h"
#include "runtime/runtime_config.h"
#include "storage/redis/redis_pubsub.h"
#include "transport/handoff_message.h"
#include "worker/worker_core.h"
#include "worker/worker_mailbox.h"
#include "worker/worker_metrics.h"

namespace navcaster::caster {

class CasterWorker {
public:
    CasterWorker(std::uint32_t worker_id, std::string runtime_id, RedisEndpoint redis);
    ~CasterWorker();

    CasterWorker(const CasterWorker &) = delete;
    CasterWorker &operator=(const CasterWorker &) = delete;

    bool start();
    void stop();

    bool post_handoff(HandoffMessage message);
    bool post_probe();
    void set_draining(bool draining);

    WorkerMetricsSnapshot snapshot() const;
    std::uint32_t id() const { return _worker_id; }
    bool running() const { return _running.load(); }
    bool ready() const { return _ready.load(); }

private:
    void thread_main();
    bool wait_until_ready();

    std::uint32_t _worker_id = 0;
    std::string _runtime_id;
    RedisEndpoint _redis_endpoint;
    std::thread _thread;
    EventBasePtr _base;
    std::unique_ptr<WorkerMailbox> _mailbox;
    std::unique_ptr<WorkerCore> _core;
    std::unique_ptr<WorkerRedisBoundary> _redis_boundary;

    mutable std::mutex _state_mutex;
    std::condition_variable _ready_cv;
    std::atomic<bool> _running{false};
    std::atomic<bool> _ready{false};
    std::atomic<bool> _stop_requested{false};
};

} // namespace navcaster::caster
