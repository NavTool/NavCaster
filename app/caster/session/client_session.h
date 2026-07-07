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

class ClientSession {
public:
    using DataCallback = std::function<void(std::uint64_t, std::string)>;
    using ClosedCallback = std::function<void(std::uint64_t)>;

    ClientSession(
        std::uint64_t session_id,
        std::uint32_t worker_id,
        HandoffMessage handoff,
        DataCallback on_data,
        ClosedCallback on_closed);
    ~ClientSession();

    ClientSession(const ClientSession &) = delete;
    ClientSession &operator=(const ClientSession &) = delete;

    bool start(event_base *base);
    std::string consume_initial_bytes();
    bool write_bytes(const char *data, std::size_t length);
    void close();

    std::size_t pending_output_bytes() const;
    const std::string &mount() const { return handoff_.connect_info.mount; }
    const std::string &remote_addr() const { return handoff_.connect_info.remote_addr; }
    std::uint16_t remote_port() const { return handoff_.connect_info.remote_port; }
    std::uint64_t session_id() const { return session_id_; }
    std::uint64_t bytes_out() const { return bytes_out_; }
    bool input_failed() const { return chunked_decoder_.failed(); }
    bool input_complete() const { return chunked_decoder_.complete(); }
    bool response_chunked() const { return response_chunked_; }

private:
    std::string decode_incoming(std::string data);
    void handle_read();
    void handle_event(short events);
    void release_bev();
    void notify_closed();

    static void on_read(bufferevent *bev, void *arg);
    static void on_event(bufferevent *bev, short events, void *arg);

    std::uint64_t session_id_ = 0;
    std::uint32_t worker_id_ = 0;
    HandoffMessage handoff_;
    DataCallback on_data_;
    ClosedCallback on_closed_;
    HttpChunkedDecoder chunked_decoder_;
    bufferevent *bev_ = nullptr;
    bool closed_notified_ = false;
    bool request_body_chunked_ = false;
    bool response_chunked_ = false;
    std::uint64_t bytes_out_ = 0;
};

} // namespace navcaster::caster
