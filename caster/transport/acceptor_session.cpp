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

} // namespace

ConnectInfo AcceptorSessionParser::parse_request_head(const std::string &request_head) const
{
    ConnectInfo info;
    std::istringstream input(request_head);
    std::string method;
    std::string target;
    std::string third;
    input >> method >> target;

    if (method == "GET") {
        info.type = ConnectType::Client;
        info.mount = trim_mount(target);
    } else if (method == "POST" || method == "SOURCE") {
        info.type = ConnectType::Source;
        if (method == "SOURCE" && !target.empty() && target.front() != '/' && input >> third) {
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
        std::string key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (key == "authorization") {
            info.auth_header = line.substr(colon + 1);
        }
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
    : sink_(std::move(sink))
{
    const auto peer = peer_address_from_sockaddr(address, socklen);
    message_.fd = fd;
    message_.accepted_at_ms = steady_time_ms();
    message_.remote_addr = peer.host;
    message_.remote_port = peer.port;
    message_.connect_info.remote_addr = peer.host;
    message_.connect_info.remote_port = peer.port;
}

AcceptorSession::~AcceptorSession()
{
    if (bev_) {
        bufferevent_free(bev_);
        bev_ = nullptr;
    }
}

bool AcceptorSession::attach(event_base *base)
{
    if (!base || message_.fd < 0) {
        return false;
    }

    bev_ = bufferevent_socket_new(base, message_.fd, BEV_OPT_DEFER_CALLBACKS);
    if (!bev_) {
        return false;
    }

    bufferevent_setcb(bev_, &AcceptorSession::on_read, nullptr, &AcceptorSession::on_event, this);
    bufferevent_enable(bev_, EV_READ);
    return true;
}

void AcceptorSession::read_available()
{
    evbuffer *input = bufferevent_get_input(bev_);
    const auto length = evbuffer_get_length(input);
    if (length > 0) {
        std::string chunk(length, '\0');
        evbuffer_remove(input, &chunk[0], length);
        read_buffer_ += chunk;
    }

    if (read_buffer_.size() > kMaxHeaderBytes) {
        log_warn("acceptor session header exceeds max bytes");
        close_and_destroy();
        return;
    }

    const auto header_end = read_buffer_.find("\r\n\r\n");
    const auto alt_header_end = read_buffer_.find("\n\n");
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

    const std::string request_head = read_buffer_.substr(0, end + delimiter_size);
    message_.initial_bytes = read_buffer_.substr(end + delimiter_size);
    message_.connect_info = parser_.parse_request_head(request_head);
    message_.connect_info.remote_addr = message_.remote_addr;
    message_.connect_info.remote_port = message_.remote_port;
    dispatch_or_close();
}

void AcceptorSession::dispatch_or_close()
{
    bufferevent_disable(bev_, EV_READ | EV_WRITE);
    const auto fd = bufferevent_getfd(bev_);
    message_.fd = fd;

    if (sink_ && sink_(std::move(message_))) {
        message_.fd = -1;
        destroy_after_handoff();
        return;
    }

    close_and_destroy();
}

void AcceptorSession::close_and_destroy()
{
    if (message_.fd >= 0) {
        close_socket(message_.fd);
        message_.fd = -1;
    }
    delete this;
}

void AcceptorSession::destroy_after_handoff()
{
    if (bev_) {
        bufferevent_setfd(bev_, -1);
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
