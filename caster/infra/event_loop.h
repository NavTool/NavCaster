#pragma once

#include <memory>

#include <event2/event.h>

namespace navcaster::caster {

class EventThreadingGuard {
public:
    EventThreadingGuard();

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

struct EventBaseDeleter {
    void operator()(event_base *base) const;
};

struct EventDeleter {
    void operator()(event *ev) const;
};

using EventBasePtr = std::unique_ptr<event_base, EventBaseDeleter>;
using EventPtr = std::unique_ptr<event, EventDeleter>;

EventBasePtr make_event_base();

} // namespace navcaster::caster
