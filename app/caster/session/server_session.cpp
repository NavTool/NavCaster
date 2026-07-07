#include "session/server_session.h"

#include <event2/buffer.h>

#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {
namespace {

constexpr const char *kNtripOkResponse = "ICY 200 OK\r\n\r\n";

std::string server_ok_response(const ConnectInfo &info)
{
    if (!info.ntrip2) {
        return kNtripOkResponse;
    }
    return "HTTP/1.1 200 OK\r\n"
           "Server: NavCaster\r\n"
           "Ntrip-Version: Ntrip/2.0\r\n"
           "Connection: close\r\n"
           "\r\n";
}

} // namespace

ServerSession::ServerSession(
    std::string connect_key,
    std::uint32_t worker_id,
    HandoffMessage handoff,
    DataCallback on_data,
    ClosedCallback on_closed)
    : _connect_key(std::move(connect_key)),
      _worker_id(worker_id),
      _handoff(std::move(handoff)),
      _on_data(std::move(on_data)),
      _on_closed(std::move(on_closed)),
      _request_body_chunked(_handoff.connect_info.request_body_chunked)
{
    if (_handoff.connect_key.empty()) {
        _handoff.connect_key = _connect_key;
    }
}

ServerSession::~ServerSession()
{
    release_bev();
    if (_handoff.fd >= 0) {
        close_socket(_handoff.fd);
        _handoff.fd = -1;
    }
}

bool ServerSession::start(event_base *base)
{
    if (!base || _handoff.fd < 0) {
        return false;
    }

    _bev = bufferevent_socket_new(base, _handoff.fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (!_bev) {
        return false;
    }
    _handoff.fd = -1;

    bufferevent_setcb(_bev, &ServerSession::on_read, nullptr, &ServerSession::on_event, this);
    bufferevent_enable(_bev, EV_READ | EV_WRITE);

    const auto response = server_ok_response(_handoff.connect_info);
    if (bufferevent_write(_bev, response.data(), response.size()) != 0) {
        log_warn("server session failed to write NTRIP response worker=" + std::to_string(_worker_id));
        return false;
    }
    return true;
}

std::string ServerSession::consume_initial_bytes()
{
    std::string data = std::move(_handoff.initial_bytes);
    _handoff.initial_bytes.clear();
    return decode_incoming(std::move(data));
}

void ServerSession::close()
{
    _closed_notified = true;
    release_bev();
}

std::string ServerSession::decode_incoming(std::string data)
{
    if (data.empty()) {
        return {};
    }
    if (!_request_body_chunked) {
        return data;
    }
    auto decoded = _chunked_decoder.feed(data);
    if (_chunked_decoder.failed()) {
        log_warn("server session received invalid chunked body worker=" + std::to_string(_worker_id));
    }
    return decoded;
}

void ServerSession::handle_read()
{
    if (!_bev) {
        return;
    }

    evbuffer *input = bufferevent_get_input(_bev);
    const auto length = evbuffer_get_length(input);
    if (length == 0) {
        return;
    }

    std::string data(length, '\0');
    evbuffer_remove(input, &data[0], length);
    data = decode_incoming(std::move(data));
    _bytes_in += static_cast<std::uint64_t>(data.size());
    if (_on_data && !data.empty()) {
        _on_data(_connect_key, std::move(data));
    }
    if (_chunked_decoder.failed() || _chunked_decoder.complete()) {
        release_bev();
        notify_closed();
    }
}

void ServerSession::handle_event(short events)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        release_bev();
        notify_closed();
    }
}

void ServerSession::release_bev()
{
    if (_bev) {
        bufferevent_setcb(_bev, nullptr, nullptr, nullptr, nullptr);
        bufferevent_free(_bev);
        _bev = nullptr;
    }
}

void ServerSession::notify_closed()
{
    if (_closed_notified) {
        return;
    }
    _closed_notified = true;
    if (_on_closed) {
        _on_closed(_connect_key);
    }
}

void ServerSession::on_read(bufferevent *, void *arg)
{
    auto *session = static_cast<ServerSession *>(arg);
    if (session) {
        session->handle_read();
    }
}

void ServerSession::on_event(bufferevent *, short events, void *arg)
{
    auto *session = static_cast<ServerSession *>(arg);
    if (session) {
        session->handle_event(events);
    }
}

} // namespace navcaster::caster
