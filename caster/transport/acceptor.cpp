#include "transport/acceptor.h"

#include <cstring>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

#include <event2/listener.h>

#include "infra/logger.h"
#include "infra/socket_util.h"
#include "transport/acceptor_session.h"

namespace navcaster::caster {

Acceptor::Acceptor(RuntimeConfig config, HandoffSink sink)
    : config_(std::move(config)), sink_(std::move(sink))
{
}

Acceptor::~Acceptor()
{
    stop();
}

bool Acceptor::start(event_base *base)
{
    if (!base || listener_) {
        return listener_ != nullptr;
    }

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(config_.listen_port);
    if (evutil_inet_pton(AF_INET, config_.listen_host.c_str(), &sin.sin_addr) != 1) {
        log_error("invalid acceptor bind address: " + config_.listen_host);
        return false;
    }

    listener_ = evconnlistener_new_bind(
        base,
        &Acceptor::on_accept,
        this,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        -1,
        reinterpret_cast<sockaddr *>(&sin),
        sizeof(sin));

    if (!listener_) {
        log_error("failed to bind acceptor on " + config_.listen_host + ":" + std::to_string(config_.listen_port));
        return false;
    }

    evconnlistener_set_error_cb(listener_, &Acceptor::on_accept_error);
    return true;
}

void Acceptor::stop()
{
    if (listener_) {
        evconnlistener_free(listener_);
        listener_ = nullptr;
    }
}

void Acceptor::on_accept(evconnlistener *listener, evutil_socket_t fd, sockaddr *address, int socklen, void *arg)
{
    auto *acceptor = static_cast<Acceptor *>(arg);
    if (!acceptor) {
        close_socket(fd);
        return;
    }

    event_base *base = evconnlistener_get_base(listener);
    if (!AcceptorSession::start(base, fd, address, socklen, acceptor->sink_)) {
        close_socket(fd);
    }
}

void Acceptor::on_accept_error(evconnlistener *, void *)
{
    const int err = EVUTIL_SOCKET_ERROR();
    log_warn(std::string("acceptor error: ") + evutil_socket_error_to_string(err));
}

} // namespace navcaster::caster
