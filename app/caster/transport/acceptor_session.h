#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

#include "domain/connect_info.h"
#include "transport/handoff_message.h"

namespace navcaster::caster {

class AcceptorSessionParser {
public:
    ConnectInfo parse_request_head(const std::string &request_head) const;
};

class AcceptorSession {
public:
    using HandoffSink = std::function<bool(HandoffMessage)>;

    static bool start(event_base *base, evutil_socket_t fd, sockaddr *address, int socklen, HandoffSink sink);

private:
    AcceptorSession(evutil_socket_t fd, sockaddr *address, int socklen, HandoffSink sink);
    ~AcceptorSession();

    AcceptorSession(const AcceptorSession &) = delete;
    AcceptorSession &operator=(const AcceptorSession &) = delete;

    bool attach(event_base *base);
    void read_available();
    void dispatch_or_close();
    void close_and_destroy();
    void destroy_after_handoff();

    static void on_read(bufferevent *bev, void *arg);
    static void on_event(bufferevent *bev, short events, void *arg);

    HandoffSink _sink;
    AcceptorSessionParser _parser;
    bufferevent *_bev = nullptr;
    HandoffMessage _message;
    std::string _read_buffer;
};

} // namespace navcaster::caster
