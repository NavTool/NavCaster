#include "session/source_session.h"

#include <sstream>

#include <event2/buffer.h>

#include "domain/http_chunked_codec.h"
#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {
namespace {

std::string source_table_response(const ConnectInfo &info, const std::string &body)
{
    if (info.ntrip2) {
        const bool chunked = info.accepts_chunked_response;
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Server: NavCaster\r\n"
                 << "Ntrip-Version: Ntrip/2.0\r\n"
                 << "Content-Type: text/plain\r\n";
        if (chunked) {
            response << "Transfer-Encoding: chunked\r\n";
        } else {
            response << "Content-Length: " << body.size() << "\r\n";
        }
        response << "Connection: close\r\n"
                 << "\r\n";
        if (chunked) {
            response << encode_http_chunk(body) << encode_http_last_chunk();
        } else {
            response << body;
        }
        return response.str();
    }

    std::ostringstream response;
    response << "SOURCETABLE 200 OK\r\n"
             << "Server: NavCaster\r\n"
             << "Content-Type: text/plain\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    return response.str();
}

} // namespace

SourceSession::SourceSession(
    std::uint64_t session_id,
    HandoffMessage handoff,
    BodyProvider body_provider,
    ClosedCallback on_closed)
    : _session_id(session_id),
      _handoff(std::move(handoff)),
      _body_provider(std::move(body_provider)),
      _on_closed(std::move(on_closed))
{
}

SourceSession::~SourceSession()
{
    release_bev();
    if (_handoff.fd >= 0) {
        close_socket(_handoff.fd);
        _handoff.fd = -1;
    }
}

bool SourceSession::start(event_base *base)
{
    if (!base || _handoff.fd < 0 || !_body_provider) {
        return false;
    }

    _bev = bufferevent_socket_new(base, _handoff.fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (!_bev) {
        return false;
    }
    _handoff.fd = -1;

    bufferevent_setcb(_bev, nullptr, &SourceSession::on_write, &SourceSession::on_event, this);

    const auto body = _body_provider(_handoff.connect_info);
    const auto response = source_table_response(_handoff.connect_info, body);
    if (bufferevent_write(_bev, response.data(), response.size()) != 0) {
        log_warn("source session failed to write source table response");
        return false;
    }

    bufferevent_enable(_bev, EV_WRITE);
    return true;
}

void SourceSession::close()
{
    _closed_notified = true;
    release_bev();
    if (_handoff.fd >= 0) {
        close_socket(_handoff.fd);
        _handoff.fd = -1;
    }
}

void SourceSession::handle_write()
{
    if (!_bev) {
        return;
    }

    if (evbuffer_get_length(bufferevent_get_output(_bev)) != 0) {
        return;
    }

    release_bev();
    notify_closed();
}

void SourceSession::handle_event(short events)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        release_bev();
        notify_closed();
    }
}

void SourceSession::release_bev()
{
    if (_bev) {
        bufferevent_setcb(_bev, nullptr, nullptr, nullptr, nullptr);
        bufferevent_free(_bev);
        _bev = nullptr;
    }
}

void SourceSession::notify_closed()
{
    if (_closed_notified) {
        return;
    }
    _closed_notified = true;
    if (_on_closed) {
        _on_closed(_session_id);
    }
}

void SourceSession::on_write(bufferevent *, void *arg)
{
    auto *session = static_cast<SourceSession *>(arg);
    if (session) {
        session->handle_write();
    }
}

void SourceSession::on_event(bufferevent *, short events, void *arg)
{
    auto *session = static_cast<SourceSession *>(arg);
    if (session) {
        session->handle_event(events);
    }
}

} // namespace navcaster::caster
