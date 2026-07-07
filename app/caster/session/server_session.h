#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include "domain/http_chunked_codec.h"
#include "transport/handoff_message.h"

namespace navcaster::caster {

class ServerSession {
public:
    using DataCallback = std::function<void(std::uint64_t, std::string)>;
    using ClosedCallback = std::function<void(std::uint64_t)>;

    ServerSession(
        std::uint64_t session_id,
        std::uint32_t worker_id,
        HandoffMessage handoff,
        DataCallback on_data,
        ClosedCallback on_closed);
    ~ServerSession();

    ServerSession(const ServerSession &) = delete;
    ServerSession &operator=(const ServerSession &) = delete;

    bool start(event_base *base);
    std::string consume_initial_bytes();
    void close();

    const std::string &mount() const { return _handoff.connect_info.mount; }
    std::uint64_t session_id() const { return _session_id; }
    std::uint64_t bytes_in() const { return _bytes_in; }
    bool input_failed() const { return _chunked_decoder.failed(); }
    bool input_complete() const { return _chunked_decoder.complete(); }

private:
    std::string decode_incoming(std::string data);
    void handle_read();
    void handle_event(short events);
    void release_bev();
    void notify_closed();

    static void on_read(bufferevent *bev, void *arg);
    static void on_event(bufferevent *bev, short events, void *arg);

    std::uint64_t _session_id = 0;
    std::uint32_t _worker_id = 0;
    HandoffMessage _handoff;
    DataCallback _on_data;
    ClosedCallback _on_closed;
    HttpChunkedDecoder _chunked_decoder;
    bufferevent *_bev = nullptr;
    bool _closed_notified = false;
    bool _request_body_chunked = false;
    std::uint64_t _bytes_in = 0;
};

} // namespace navcaster::caster
