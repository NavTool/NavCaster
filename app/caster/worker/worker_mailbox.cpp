#include "worker/worker_mailbox.h"

#include <utility>

namespace navcaster::caster {

WorkerMailbox::~WorkerMailbox()
{
    detach();
}

bool WorkerMailbox::attach(event_base *base)
{
    if (!base || _attached) {
        return _attached;
    }

    _base = base;
    _attached = true;
    return _attached;
}

bool WorkerMailbox::post(Message message)
{
    if (!message) {
        return false;
    }

    event_base *base = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_attached || !_base) {
            return false;
        }
        _queue.push(std::move(message));
        ++_posted_count;
        base = _base;
    }

    return event_base_once(base, -1, EV_TIMEOUT, &WorkerMailbox::on_mailbox_event, this, nullptr) == 0;
}

void WorkerMailbox::drain()
{
    std::queue<Message> local;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::swap(local, _queue);
    }

    while (!local.empty()) {
        auto message = std::move(local.front());
        local.pop();
        message();
    }
}

void WorkerMailbox::detach()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _attached = false;
    _base = nullptr;
    std::queue<Message> empty;
    std::swap(_queue, empty);
}

std::uint64_t WorkerMailbox::posted_count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _posted_count;
}

void WorkerMailbox::on_mailbox_event(evutil_socket_t, short, void *arg)
{
    auto *mailbox = static_cast<WorkerMailbox *>(arg);
    if (mailbox) {
        mailbox->drain();
    }
}

} // namespace navcaster::caster
