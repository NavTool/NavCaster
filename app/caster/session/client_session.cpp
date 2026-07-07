#include "session/client_session.h"

#include <event2/buffer.h>

#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {
namespace {

constexpr const char *kNtripOkResponse = "ICY 200 OK\r\n\r\n";

std::string client_ok_response(const ConnectInfo &info)
{
    if (!info.ntrip2) {
        return kNtripOkResponse;
    }

    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Server: NavCaster\r\n"
        "Ntrip-Version: Ntrip/2.0\r\n"
        "Content-Type: gnss/data\r\n";
    if (info.accepts_chunked_response) {
        response += "Transfer-Encoding: chunked\r\n";
    }
    response += "Connection: close\r\n\r\n";
    return response;
}

} // namespace

ClientSession::ClientSession(
    std::uint64_t session_id,
    std::uint32_t worker_id,
    HandoffMessage handoff,
    DataCallback on_data,
    ClosedCallback on_closed)
    : session_id_(session_id),
      worker_id_(worker_id),
      handoff_(std::move(handoff)),
      on_data_(std::move(on_data)),
      on_closed_(std::move(on_closed)),
      request_body_chunked_(handoff_.connect_info.request_body_chunked),
      response_chunked_(handoff_.connect_info.accepts_chunked_response)
{
}

ClientSession::~ClientSession()
{
    release_bev();
    if (handoff_.fd >= 0) {
        close_socket(handoff_.fd);
        handoff_.fd = -1;
    }
}

bool ClientSession::start(event_base *base)
{
    if (!base || handoff_.fd < 0) {
        return false;
    }

    bev_ = bufferevent_socket_new(base, handoff_.fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (!bev_) {
        return false;
    }
    handoff_.fd = -1;

    bufferevent_setcb(bev_, &ClientSession::on_read, nullptr, &ClientSession::on_event, this);
    bufferevent_enable(bev_, EV_READ | EV_WRITE);

    const auto response = client_ok_response(handoff_.connect_info);
    if (bufferevent_write(bev_, response.data(), response.size()) != 0) {
        log_warn("client session failed to write NTRIP response worker=" + std::to_string(worker_id_));
        return false;
    }
    return true;
}

std::string ClientSession::consume_initial_bytes()
{
    std::string data = std::move(handoff_.initial_bytes);
    handoff_.initial_bytes.clear();
    return decode_incoming(std::move(data));
}

bool ClientSession::write_bytes(const char *data, std::size_t length)
{
    if (!bev_ || !data || length == 0) {
        return false;
    }
    const auto output = response_chunked_ ? encode_http_chunk(data, length) : std::string(data, length);
    if (output.empty() || bufferevent_write(bev_, output.data(), output.size()) != 0) {
        return false;
    }
    bytes_out_ += static_cast<std::uint64_t>(length);
    return true;
}

void ClientSession::close()
{
    closed_notified_ = true;
    release_bev();
}

std::size_t ClientSession::pending_output_bytes() const
{
    if (!bev_) {
        return 0;
    }
    return evbuffer_get_length(bufferevent_get_output(bev_));
}

std::string ClientSession::decode_incoming(std::string data)
{
    if (data.empty()) {
        return {};
    }
    if (!request_body_chunked_) {
        return data;
    }
    auto decoded = chunked_decoder_.feed(data);
    if (chunked_decoder_.failed()) {
        log_warn("client session received invalid chunked body worker=" + std::to_string(worker_id_));
    }
    return decoded;
}

void ClientSession::handle_read()
{
    if (!bev_) {
        return;
    }
    evbuffer *input = bufferevent_get_input(bev_);
    const auto length = evbuffer_get_length(input);
    if (length == 0) {
        return;
    }

    std::string data(length, '\0');
    evbuffer_remove(input, &data[0], length);
    data = decode_incoming(std::move(data));
    if (on_data_ && !data.empty()) {
        on_data_(session_id_, std::move(data));
    }
    if (chunked_decoder_.failed() || chunked_decoder_.complete()) {
        release_bev();
        notify_closed();
    }
}

void ClientSession::handle_event(short events)
{
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        release_bev();
        notify_closed();
    }
}

void ClientSession::release_bev()
{
    if (bev_) {
        bufferevent_setcb(bev_, nullptr, nullptr, nullptr, nullptr);
        bufferevent_free(bev_);
        bev_ = nullptr;
    }
}

void ClientSession::notify_closed()
{
    if (closed_notified_) {
        return;
    }
    closed_notified_ = true;
    if (on_closed_) {
        on_closed_(session_id_);
    }
}

void ClientSession::on_read(bufferevent *, void *arg)
{
    auto *session = static_cast<ClientSession *>(arg);
    if (session) {
        session->handle_read();
    }
}

void ClientSession::on_event(bufferevent *, short events, void *arg)
{
    auto *session = static_cast<ClientSession *>(arg);
    if (session) {
        session->handle_event(events);
    }
}

} // namespace navcaster::caster
