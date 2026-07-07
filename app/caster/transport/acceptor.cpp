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
#include <event2/util.h>

#include "infra/logger.h"
#include "infra/socket_util.h"
#include "transport/acceptor_session.h"

namespace navcaster::caster {
namespace {

constexpr int kListenBacklog = 128;

} // namespace

Acceptor::Acceptor(RuntimeConfig config, HandoffSink sink)
    : _config(std::move(config)), _sink(std::move(sink))
{
}

Acceptor::~Acceptor()
{
    stop();
}

bool Acceptor::start(event_base *base)
{
    if (!base || _listener) {
        return _listener != nullptr;
    }

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(_config.listen_port);
    if (evutil_inet_pton(AF_INET, _config.listen_host.c_str(), &sin.sin_addr) != 1) {
        log_error("invalid acceptor bind address: " + _config.listen_host);
        return false;
    }

    evutil_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        const int err = EVUTIL_SOCKET_ERROR();
        log_error("failed to create acceptor socket on " + _config.listen_host + ":" + std::to_string(_config.listen_port) +
                  " error=" + evutil_socket_error_to_string(err));
        return false;
    }

    evutil_make_listen_socket_reuseable(fd);
    if (bind(fd, reinterpret_cast<sockaddr *>(&sin), sizeof(sin)) != 0) {
        const int err = EVUTIL_SOCKET_ERROR();
        log_error("failed to bind acceptor on " + _config.listen_host + ":" + std::to_string(_config.listen_port) +
                  " error=" + evutil_socket_error_to_string(err));
        close_socket(fd);
        return false;
    }
    if (listen(fd, kListenBacklog) != 0) {
        const int err = EVUTIL_SOCKET_ERROR();
        log_error("failed to listen acceptor on " + _config.listen_host + ":" + std::to_string(_config.listen_port) +
                  " error=" + evutil_socket_error_to_string(err));
        close_socket(fd);
        return false;
    }
    evutil_make_socket_nonblocking(fd);

    _listener = evconnlistener_new(
        base,
        &Acceptor::on_accept,
        this,
        LEV_OPT_CLOSE_ON_FREE,
        kListenBacklog,
        fd);

    if (!_listener) {
        const int err = EVUTIL_SOCKET_ERROR();
        log_error("failed to bind acceptor on " + _config.listen_host + ":" + std::to_string(_config.listen_port) +
                  " error=" + evutil_socket_error_to_string(err));
        close_socket(fd);
        return false;
    }

    evconnlistener_set_error_cb(_listener, &Acceptor::on_accept_error);
    return true;
}

void Acceptor::stop()
{
    if (_listener) {
        evconnlistener_free(_listener);
        _listener = nullptr;
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
    if (!AcceptorSession::start(base, fd, address, socklen, acceptor->_sink)) {
        close_socket(fd);
    }
}

void Acceptor::on_accept_error(evconnlistener *, void *)
{
    const int err = EVUTIL_SOCKET_ERROR();
    log_warn(std::string("acceptor error: ") + evutil_socket_error_to_string(err));
}

} // namespace navcaster::caster
