#include "session/source_session.h"

#include <event2/buffer.h>

#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {
namespace {

constexpr const char *kNtripOkResponse = "ICY 200 OK\r\n\r\n";

} // namespace

SourceSession::SourceSession(
    std::uint64_t session_id,
    std::uint32_t worker_id,
    HandoffMessage handoff,
    DataCallback on_data,
    ClosedCallback on_closed)
    : session_id_(session_id),
      worker_id_(worker_id),
      handoff_(std::move(handoff)),
      on_data_(std::move(on_data)),
      on_closed_(std::move(on_closed))
{
}

SourceSession::~SourceSession()
{
    release_bev();
    if (handoff_.fd >= 0) {
        close_socket(handoff_.fd);
        handoff_.fd = -1;
    }
}

bool SourceSession::start(event_base *base)
{
    if (!base || handoff_.fd < 0) {
        return false;
    }

    bev_ = bufferevent_socket_new(base, handoff_.fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (!bev_) {
        return false;
    }
    handoff_.fd = -1;

    bufferevent_setcb(bev_, &SourceSession::on_read, nullptr, &SourceSession::on_event, this);
    bufferevent_enable(bev_, EV_READ | EV_WRITE);

    if (bufferevent_write(bev_, kNtripOkResponse, std::char_traits<char>::length(kNtripOkResponse)) != 0) {
        log_warn("source session failed to write NTRIP response worker=" + std::to_string(worker_id_));
        return false;
    }
    return true;
}

void SourceSession::close()
{
    closed_notified_ = true;
    release_bev();
}

void SourceSession::handle_read()
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
    bytes_in_ += static_cast<std::uint64_t>(data.size());
    if (on_data_) {
        on_data_(session_id_, std::move(data));
    }
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
    if (bev_) {
        bufferevent_setcb(bev_, nullptr, nullptr, nullptr, nullptr);
        bufferevent_free(bev_);
        bev_ = nullptr;
    }
}

void SourceSession::notify_closed()
{
    if (closed_notified_) {
        return;
    }
    closed_notified_ = true;
    if (on_closed_) {
        on_closed_(session_id_);
    }
}

void SourceSession::on_read(bufferevent *, void *arg)
{
    auto *session = static_cast<SourceSession *>(arg);
    if (session) {
        session->handle_read();
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
