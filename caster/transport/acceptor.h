#pragma once

#include <functional>
#include <memory>
#include <string>

#include <event2/listener.h>

#include "infra/event_loop.h"
#include "runtime/runtime_config.h"
#include "transport/handoff_message.h"

namespace navcaster::caster {

class Acceptor {
public:
    using HandoffSink = std::function<bool(HandoffMessage)>;

    Acceptor(RuntimeConfig config, HandoffSink sink);
    ~Acceptor();

    Acceptor(const Acceptor &) = delete;
    Acceptor &operator=(const Acceptor &) = delete;

    bool start(event_base *base);
    void stop();
    bool running() const { return listener_ != nullptr; }

private:
    static void on_accept(evconnlistener *listener, evutil_socket_t fd, sockaddr *address, int socklen, void *arg);
    static void on_accept_error(evconnlistener *listener, void *arg);

    RuntimeConfig config_;
    HandoffSink sink_;
    evconnlistener *listener_ = nullptr;
};

} // namespace navcaster::caster
