#pragma once

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Represents one SSE client connection
struct SseClient
{
    evhttp_request *req = nullptr;
    std::string subscribed_channels; // comma-separated, or "*" for all
};

/**
 * sse_manager: Manages Server-Sent Events connections and broadcasts.
 *
 * Data flow:
 *   1. Client opens GET /api/events/stream → registered as SseClient
 *   2. A periodic timer fires on the event loop, calling data_fetcher callbacks
 *   3. Changed data is broadcast as SSE events to all connected clients
 *
 * SSE format per event:
 *   event: <channel>\n
 *   data: <json>\n\n
 */
class sse_manager
{
public:
    sse_manager();
    ~sse_manager();

    // Initialize with event_base and update interval (seconds)
    int init(event_base *base, int update_interval_sec = 2);
    void stop();

    // Register a data channel with a fetcher function.
    // The fetcher returns a JSON object (the full HGETALL result).
    // On each tick, if the result differs from cached, an SSE event is sent.
    using DataFetcher = std::function<json()>;
    void register_channel(const std::string &channel, DataFetcher fetcher);

    // Add a new SSE client (called from the SSE route handler)
    void add_client(evhttp_request *req, const std::string &channels = "*");

    // Remove a disconnected client
    void remove_client(evhttp_request *req);

    // Get connected client count
    size_t client_count() const;

    // Broadcast an event to all clients (or filtered by channel)
    void broadcast(const std::string &event_name, const std::string &data);

private:
    // Timer callback — runs on event loop thread
    static void on_timer(evutil_socket_t fd, short what, void *arg);
    void poll_and_broadcast();

    // Connection close callback
    static void on_client_close(evhttp_connection *conn, void *arg);

    // Send SSE-formatted message to one client
    static void send_sse_event(evhttp_request *req, const std::string &event_name, const std::string &data);

    // Send SSE comment (keepalive)
    static void send_sse_comment(evhttp_request *req, const std::string &comment);

private:
    event_base *_base = nullptr;
    event *_timer_event = nullptr;
    int _interval_sec = 2;

    // Connected SSE clients
    std::vector<SseClient> _clients;

    // Registered channels and their fetchers
    struct ChannelInfo
    {
        DataFetcher fetcher;
        json cached_data; // last known state
    };
    std::unordered_map<std::string, ChannelInfo> _channels;
};
