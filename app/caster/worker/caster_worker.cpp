#include "worker/caster_worker.h"

#include <chrono>
#include <sstream>
#include <utility>

#include <event2/event.h>

#include "infra/logger.h"

namespace navcaster::caster {

CasterWorker::CasterWorker(
    std::uint32_t worker_id,
    std::string runtime_id,
    RedisEndpoint redis,
    std::shared_ptr<ClusterSourcetableCache> sourcetable_cache)
    : _worker_id(worker_id),
      _runtime_id(std::move(runtime_id)),
      _redis_endpoint(std::move(redis)),
      _sourcetable_cache(std::move(sourcetable_cache))
{
}

CasterWorker::~CasterWorker()
{
    stop();
}

bool CasterWorker::start()
{
    if (_running.load()) {
        return true;
    }

    _stop_requested.store(false);
    _running.store(true);
    _thread = std::thread(&CasterWorker::thread_main, this);
    if (wait_until_ready()) {
        return true;
    }

    stop();
    return false;
}

void CasterWorker::stop()
{
    if (!_running.load() && !_thread.joinable()) {
        return;
    }

    _stop_requested.store(true);
    if (_base) {
        event_base_loopbreak(_base.get());
    }
    if (_thread.joinable()) {
        _thread.join();
    }
}

bool CasterWorker::post_handoff(HandoffMessage message)
{
    if (!_ready.load() || !_mailbox) {
        return false;
    }

    return _mailbox->post([this, message = std::move(message)]() mutable {
        if (_core) {
            _core->accept_handoff(std::move(message));
        }
    });
}

bool CasterWorker::post_probe()
{
    if (!_ready.load() || !_mailbox) {
        return false;
    }

    return _mailbox->post([]() {});
}

void CasterWorker::set_draining(bool draining)
{
    if (!_ready.load() || !_mailbox) {
        return;
    }

    _mailbox->post([this, draining]() {
        if (_core) {
            _core->set_draining(draining);
        }
    });
}

WorkerMetricsSnapshot CasterWorker::snapshot() const
{
    WorkerMetricsSnapshot snapshot;
    if (_core) {
        snapshot = _core->snapshot();
    } else {
        snapshot.worker_id = _worker_id;
    }
    snapshot.running = _running.load();
    if (_mailbox) {
        snapshot.mailbox_messages = _mailbox->posted_count();
    }
    return snapshot;
}

void CasterWorker::thread_main()
{
    _base = make_event_base();
    if (!_base) {
        log_error("worker " + std::to_string(_worker_id) + " failed to create event_base");
        _running.store(false);
        {
            std::lock_guard<std::mutex> lock(_state_mutex);
            _ready.store(false);
        }
        _ready_cv.notify_all();
        return;
    }

    _redis_boundary = std::make_unique<WorkerRedisBoundary>(_runtime_id, _worker_id, _redis_endpoint.host, _redis_endpoint.port);
    _core = std::make_unique<WorkerCore>(_worker_id, _base.get(), _redis_boundary.get(), _sourcetable_cache);
    _mailbox = std::make_unique<WorkerMailbox>();
    if (!_mailbox->attach(_base.get())) {
        log_error("worker " + std::to_string(_worker_id) + " failed to attach mailbox");
        _running.store(false);
        {
            std::lock_guard<std::mutex> lock(_state_mutex);
            _ready.store(false);
        }
        _ready_cv.notify_all();
        return;
    }

    _redis_boundary->start(
        _base.get(),
        [this](std::string origin_runtime_id, std::string mount, std::string payload) {
            if (_core) {
                _core->handle_redis_mount_data(std::move(origin_runtime_id), std::move(mount), std::move(payload));
            }
        },
        [this](std::string operation) {
            if (_core) {
                _core->handle_redis_error(operation);
            }
        },
        [this](std::string payload) {
            if (_core) {
                _core->handle_sourcetable_snapshot(std::move(payload));
            }
        });

    {
        std::lock_guard<std::mutex> lock(_state_mutex);
        _ready.store(true);
    }
    _ready_cv.notify_all();

    event_base_loop(_base.get(), EVLOOP_NO_EXIT_ON_EMPTY);

    if (_mailbox) {
        _mailbox->detach();
    }
    if (_redis_boundary) {
        _redis_boundary->stop();
    }
    _mailbox.reset();
    _core.reset();
    _redis_boundary.reset();
    _base.reset();
    _ready.store(false);
    _running.store(false);
}

bool CasterWorker::wait_until_ready()
{
    std::unique_lock<std::mutex> lock(_state_mutex);
    return _ready_cv.wait_for(lock, std::chrono::seconds(5), [this]() {
        return _ready.load() || !_running.load();
    }) && _ready.load();
}

} // namespace navcaster::caster
