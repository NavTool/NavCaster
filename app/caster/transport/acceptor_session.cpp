#include "transport/acceptor_session.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include <event2/buffer.h>

#include "infra/logger.h"
#include "infra/socket_util.h"
#include "infra/timer.h"

namespace navcaster::caster {
namespace {

constexpr std::size_t kMaxHeaderBytes = 8192;

std::string trim_mount(std::string value)
{
    while (!value.empty() && (value.front() == '/' || value.front() == ' ')) {
        value.erase(value.begin());
    }
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

std::string trim_header_value(std::string value)
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.erase(value.begin());
    }
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

std::string lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_token_ci(const std::string &value, const std::string &token)
{
    return lower_copy(value).find(lower_copy(token)) != std::string::npos;
}

} // namespace

ConnectInfo AcceptorSessionParser::parse_request_head(const std::string &request_head) const
{
    ConnectInfo info;
    std::istringstream input(request_head);
    std::string method;
    std::string target;
    std::string third;
    input >> method >> target;
    input >> third;
    std::transform(method.begin(), method.end(), method.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (third.rfind("HTTP/", 0) == 0) {
        info.http_version = third;
    }

    if (method == "GET") {
        info.mount = trim_mount(target);
        info.type = info.mount.empty() ? ConnectType::SourceTable : ConnectType::Client;
    } else if (method == "POST" || method == "SOURCE") {
        info.type = ConnectType::Server;
        if (method == "SOURCE" && !target.empty() && target.front() != '/' && !third.empty() && third.rfind("HTTP/", 0) != 0) {
            target = third;
        }
        info.mount = trim_mount(target);
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = lower_copy(line.substr(0, colon));
        const std::string value = trim_header_value(line.substr(colon + 1));
        if (key == "authorization") {
            info.auth_header = value;
        } else if (key == "ntrip-gga") {
            info.initial_gga = value;
        } else if (key == "ntrip-version") {
            info.ntrip_version = value;
            info.ntrip2 = contains_token_ci(value, "ntrip/2.0");
        } else if (key == "transfer-encoding") {
            if (contains_token_ci(value, "chunked")) {
                info.request_body_chunked = true;
            }
        } else if (key == "te") {
            if (contains_token_ci(value, "chunked")) {
                info.accepts_chunked_response = true;
            }
        }
    }

    if (info.type == ConnectType::SourceTable || info.type == ConnectType::Client) {
        info.accepts_chunked_response = info.ntrip2 && (info.accepts_chunked_response || info.request_body_chunked);
        info.request_body_chunked = false;
    }

    return info;
}

bool AcceptorSession::start(event_base *base, evutil_socket_t fd, sockaddr *address, int socklen, HandoffSink sink)
{
    auto *session = new AcceptorSession(fd, address, socklen, std::move(sink));
    if (!session->attach(base)) {
        delete session;
        return false;
    }
    return true;
}

AcceptorSession::AcceptorSession(evutil_socket_t fd, sockaddr *address, int socklen, HandoffSink sink)
    : _sink(std::move(sink))
{
    const auto peer = peer_address_from_sockaddr(address, socklen);
    _message.fd = fd;
    _message.accepted_at_ms = steady_time_ms();
    _message.remote_addr = peer.host;
    _message.remote_port = peer.port;
    _message.connect_info.remote_addr = peer.host;
    _message.connect_info.remote_port = peer.port;
}

AcceptorSession::~AcceptorSession()
{
    if (_bev) {
        bufferevent_free(_bev);
        _bev = nullptr;
    }
}

bool AcceptorSession::attach(event_base *base)
{
    if (!base || _message.fd < 0) {
        return false;
    }

    _bev = bufferevent_socket_new(base, _message.fd, BEV_OPT_DEFER_CALLBACKS);
    if (!_bev) {
        return false;
    }

    bufferevent_setcb(_bev, &AcceptorSession::on_read, nullptr, &AcceptorSession::on_event, this);
    bufferevent_enable(_bev, EV_READ);
    return true;
}

void AcceptorSession::read_available()
{
    evbuffer *input = bufferevent_get_input(_bev);
    const auto length = evbuffer_get_length(input);
    if (length > 0) {
        std::string chunk(length, '\0');
        evbuffer_remove(input, &chunk[0], length);
        _read_buffer += chunk;
    }

    if (_read_buffer.size() > kMaxHeaderBytes) {
        log_warn("acceptor session header exceeds max bytes");
        close_and_destroy();
        return;
    }

    const auto header_end = _read_buffer.find("\r\n\r\n");
    const auto alt_header_end = _read_buffer.find("\n\n");
    std::size_t end = std::string::npos;
    std::size_t delimiter_size = 0;
    if (header_end != std::string::npos) {
        end = header_end;
        delimiter_size = 4;
    } else if (alt_header_end != std::string::npos) {
        end = alt_header_end;
        delimiter_size = 2;
    }

    if (end == std::string::npos) {
        return;
    }

    const std::string request_head = _read_buffer.substr(0, end + delimiter_size);
    _message.initial_bytes = _read_buffer.substr(end + delimiter_size);
    _message.connect_info = _parser.parse_request_head(request_head);
    _message.connect_info.remote_addr = _message.remote_addr;
    _message.connect_info.remote_port = _message.remote_port;
    dispatch_or_close();
}

void AcceptorSession::dispatch_or_close()
{
    bufferevent_disable(_bev, EV_READ | EV_WRITE);
    const auto fd = bufferevent_getfd(_bev);
    _message.fd = fd;

    if (_sink && _sink(std::move(_message))) {
        _message.fd = -1;
        destroy_after_handoff();
        return;
    }

    close_and_destroy();
}

void AcceptorSession::close_and_destroy()
{
    if (_message.fd >= 0) {
        close_socket(_message.fd);
        _message.fd = -1;
    }
    delete this;
}

void AcceptorSession::destroy_after_handoff()
{
    if (_bev) {
        bufferevent_setfd(_bev, -1);
    }
    delete this;
}

void AcceptorSession::on_read(bufferevent *, void *arg)
{
    auto *session = static_cast<AcceptorSession *>(arg);
    if (session) {
        session->read_available();
    }
}

void AcceptorSession::on_event(bufferevent *, short events, void *arg)
{
    auto *session = static_cast<AcceptorSession *>(arg);
    if (!session) {
        return;
    }

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        session->close_and_destroy();
    }
}

} // namespace navcaster::caster
