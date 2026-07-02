#pragma once

#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Represents one SSE client connection
struct SseClient
{
    evhttp_request *req = nullptr;
    std::unordered_set<std::string> channels; // explicit channel names
    bool wildcard = false;                    // true 表示订阅全部
};

inline void parse_sse_channels(const std::string &csv,
                               std::unordered_set<std::string> &out,
                               bool &wildcard)
{
    wildcard = false;
    out.clear();
    if (csv.empty() || csv == "*")
    {
        wildcard = true;
        return;
    }

    size_t pos = 0;
    while (pos < csv.size())
    {
        size_t comma = csv.find(',', pos);
        std::string item = csv.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t l = item.find_first_not_of(" \t");
        size_t r = item.find_last_not_of(" \t");
        if (l != std::string::npos)
            item = item.substr(l, r - l + 1);
        else
            item.clear();
        if (item == "*")
            wildcard = true;
        else if (!item.empty())
            out.insert(item);
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    if (out.empty() && !wildcard)
        wildcard = true;
}

inline bool sse_client_subscribes_to(const SseClient &client, const std::string &channel)
{
    return client.wildcard || client.channels.count(channel) > 0;
}

inline bool sse_channel_has_subscriber(const std::vector<SseClient> &clients, const std::string &channel)
{
    for (const auto &client : clients)
    {
        if (sse_client_subscribes_to(client, channel))
            return true;
    }
    return false;
}

/**
 * sse_manager: Manages Server-Sent Events connections and broadcasts.
 *
 * Data flow:
 *   1. Client opens GET /api/events/stream → registered as SseClient
 *   2. A periodic timer fires on the event loop, calling subscribed channel fetchers
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

    // 设置客户端上限 (0 = 不限)
    void set_max_clients(size_t n) { _max_clients = n; }
    size_t max_clients() const { return _max_clients; }

    // Register a data channel with a fetcher function.
    // The fetcher returns a JSON object (the full HGETALL result).
    // On each tick, subscribed channels are fetched and changed data is sent.
    using DataFetcher = std::function<json()>;
    void register_channel(const std::string &channel, DataFetcher fetcher);

    // Add a new SSE client (called from the SSE route handler).
    // Returns 0 on success, -1 if the connection cap was hit.
    int add_client(evhttp_request *req, const std::string &channels = "*");

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
    size_t _max_clients = 0; // 0 = no limit

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
