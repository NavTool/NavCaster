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
    std::uint32_t id() const { return worker_id_; }
    bool running() const { return running_.load(); }
    bool ready() const { return ready_.load(); }

private:
    void thread_main();
    bool wait_until_ready();

    std::uint32_t worker_id_ = 0;
    std::string runtime_id_;
    RedisEndpoint redis_endpoint_;
    std::thread thread_;
    EventBasePtr base_;
    std::unique_ptr<WorkerMailbox> mailbox_;
    std::unique_ptr<WorkerCore> core_;
    std::unique_ptr<WorkerRedisBoundary> redis_boundary_;

    mutable std::mutex state_mutex_;
    std::condition_variable ready_cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::atomic<bool> stop_requested_{false};
};

} // namespace navcaster::caster
