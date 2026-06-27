#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class SourceSession {
public:
    using DataCallback = std::function<void(std::uint64_t, std::string)>;
    using ClosedCallback = std::function<void(std::uint64_t)>;

    SourceSession(
        std::uint64_t session_id,
        std::uint32_t worker_id,
        HandoffMessage handoff,
        DataCallback on_data,
        ClosedCallback on_closed);
    ~SourceSession();

    SourceSession(const SourceSession &) = delete;
    SourceSession &operator=(const SourceSession &) = delete;

    bool start(event_base *base);
    void close();

    const std::string &mount() const { return handoff_.connect_info.mount; }
    std::uint64_t session_id() const { return session_id_; }
    std::uint64_t bytes_in() const { return bytes_in_; }

private:
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
    bufferevent *bev_ = nullptr;
    bool closed_notified_ = false;
    std::uint64_t bytes_in_ = 0;
};

} // namespace navcaster::caster
