#include "worker/worker_mailbox.h"

#include <utility>

namespace navcaster::caster {

WorkerMailbox::~WorkerMailbox()
{
    detach();
}

bool WorkerMailbox::attach(event_base *base)
{
    if (!base || attached_) {
        return attached_;
    }

    base_ = base;
    attached_ = true;
    return attached_;
}

bool WorkerMailbox::post(Message message)
{
    if (!message) {
        return false;
    }

    event_base *base = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!attached_ || !base_) {
            return false;
        }
        queue_.push(std::move(message));
        ++posted_count_;
        base = base_;
    }

    return event_base_once(base, -1, EV_TIMEOUT, &WorkerMailbox::on_mailbox_event, this, nullptr) == 0;
}

void WorkerMailbox::drain()
{
    std::queue<Message> local;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(local, queue_);
    }

    while (!local.empty()) {
        auto message = std::move(local.front());
        local.pop();
        message();
    }
}

void WorkerMailbox::detach()
{
    std::lock_guard<std::mutex> lock(mutex_);
    attached_ = false;
    base_ = nullptr;
    std::queue<Message> empty;
    std::swap(queue_, empty);
}

std::uint64_t WorkerMailbox::posted_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return posted_count_;
}

void WorkerMailbox::on_mailbox_event(evutil_socket_t, short, void *arg)
{
    auto *mailbox = static_cast<WorkerMailbox *>(arg);
    if (mailbox) {
        mailbox->drain();
    }
}

} // namespace navcaster::caster
