#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include "domain/connect_info.h"
#include "transport/handoff_message.h"

namespace navcaster::caster {

class SourceSession {
public:
    using BodyProvider = std::function<std::string(const ConnectInfo &)>;
    using ClosedCallback = std::function<void(const std::string &)>;

    SourceSession(
        std::string connect_key,
        HandoffMessage handoff,
        BodyProvider body_provider,
        ClosedCallback on_closed);
    ~SourceSession();

    SourceSession(const SourceSession &) = delete;
    SourceSession &operator=(const SourceSession &) = delete;

    bool start(event_base *base);
    void close();

    const std::string &connect_key() const { return _connect_key; }
    const ConnectInfo &connect_info() const { return _handoff.connect_info; }

private:
    void handle_write();
    void handle_event(short events);
    void release_bev();
    void notify_closed();

    static void on_write(bufferevent *bev, void *arg);
    static void on_event(bufferevent *bev, short events, void *arg);

    std::string _connect_key;
    HandoffMessage _handoff;
    BodyProvider _body_provider;
    ClosedCallback _on_closed;
    bufferevent *_bev = nullptr;
    bool _closed_notified = false;
};

} // namespace navcaster::caster
