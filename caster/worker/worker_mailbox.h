#pragma once

#include <functional>
#include <mutex>
#include <queue>

#include <event2/event.h>

#include "infra/event_loop.h"

namespace navcaster::caster {

class WorkerMailbox {
public:
    using Message = std::function<void()>;

    WorkerMailbox() = default;
    ~WorkerMailbox();

    WorkerMailbox(const WorkerMailbox &) = delete;
    WorkerMailbox &operator=(const WorkerMailbox &) = delete;

    bool attach(event_base *base);
    bool post(Message message);
    void drain();
    void detach();

    std::uint64_t posted_count() const;

private:
    static void on_mailbox_event(evutil_socket_t fd, short what, void *arg);

    mutable std::mutex mutex_;
    std::queue<Message> queue_;
    event_base *base_ = nullptr;
    bool attached_ = false;
    std::uint64_t posted_count_ = 0;
};

} // namespace navcaster::caster
