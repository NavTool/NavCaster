#include "infra/event_loop.h"

#include <event2/thread.h>

namespace navcaster::caster {

EventThreadingGuard::EventThreadingGuard()
{
#if defined(_WIN32)
    ok_ = evthread_use_windows_threads() == 0;
#else
    ok_ = evthread_use_pthreads() == 0;
#endif
}

void EventBaseDeleter::operator()(event_base *base) const
{
    if (base) {
        event_base_free(base);
    }
}

void EventDeleter::operator()(event *ev) const
{
    if (ev) {
        event_free(ev);
    }
}

EventBasePtr make_event_base()
{
    return EventBasePtr(event_base_new());
}

} // namespace navcaster::caster
