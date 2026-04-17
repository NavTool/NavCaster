#include "HttpServer/sse_manager.h"
#include <spdlog/spdlog.h>
#include <algorithm>

#define __class__ "sse_manager"

sse_manager::sse_manager() {}

sse_manager::~sse_manager()
{
    if (_timer_event)
    {
        event_free(_timer_event);
        _timer_event = nullptr;
    }
    // End all client connections
    for (auto &client : _clients)
    {
        if (client.req)
            evhttp_send_reply_end(client.req);
    }
    _clients.clear();
}

int sse_manager::init(event_base *base, int update_interval_sec)
{
    _base = base;
    _interval_sec = update_interval_sec;

    // Create a persistent timer event
    _timer_event = event_new(base, -1, EV_PERSIST, on_timer, this);
    if (!_timer_event)
    {
        spdlog::error("[{}:{}]: Failed to create timer event", __class__, __func__);
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = _interval_sec;
    tv.tv_usec = 0;
    event_add(_timer_event, &tv);

    spdlog::info("[{}:{}]: SSE manager initialized, interval={}s", __class__, __func__, _interval_sec);
    return 0;
}

void sse_manager::register_channel(const std::string &channel, DataFetcher fetcher)
{
    _channels[channel] = {std::move(fetcher), json::object()};
    spdlog::debug("[{}:{}]: Registered SSE channel: {}", __class__, __func__, channel);
}

void sse_manager::add_client(evhttp_request *req, const std::string &channels)
{
    // Set up SSE response headers
    evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "text/event-stream");
    evhttp_add_header(evhttp_request_get_output_headers(req), "Cache-Control", "no-cache");
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "keep-alive");
    evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Origin", "*");

    // Start chunked response
    evhttp_send_reply_start(req, 200, "OK");

    // Register close callback to clean up when client disconnects
    evhttp_connection *conn = evhttp_request_get_connection(req);
    if (conn)
    {
        evhttp_connection_set_closecb(conn, on_client_close, this);
    }

    _clients.push_back({req, channels});

    spdlog::info("[{}:{}]: SSE client connected, total={}, channels={}",
                 __class__, __func__, _clients.size(), channels);

    // Send initial snapshot of all subscribed channels
    for (auto &[name, info] : _channels)
    {
        if (channels == "*" || channels.find(name) != std::string::npos)
        {
            try
            {
                json data = info.fetcher();
                if (!data.is_null() && !data.empty())
                {
                    info.cached_data = data;
                    send_sse_event(req, name, data.dump());
                }
            }
            catch (const std::exception &e)
            {
                spdlog::warn("[{}:{}]: Failed to fetch initial data for channel {}: {}",
                             __class__, __func__, name, e.what());
            }
        }
    }
}

void sse_manager::remove_client(evhttp_request *req)
{
    _clients.erase(
        std::remove_if(_clients.begin(), _clients.end(),
                       [req](const SseClient &c)
                       { return c.req == req; }),
        _clients.end());
    spdlog::info("[{}:{}]: SSE client disconnected, total={}", __class__, __func__, _clients.size());
}

size_t sse_manager::client_count() const
{
    return _clients.size();
}

void sse_manager::broadcast(const std::string &event_name, const std::string &data)
{
    std::vector<evhttp_request *> dead_clients;

    for (auto &client : _clients)
    {
        if (client.subscribed_channels == "*" ||
            client.subscribed_channels.find(event_name) != std::string::npos)
        {
            send_sse_event(client.req, event_name, data);
        }
    }
}

void sse_manager::on_timer(evutil_socket_t /*fd*/, short /*what*/, void *arg)
{
    auto *self = static_cast<sse_manager *>(arg);
    self->poll_and_broadcast();
}

void sse_manager::poll_and_broadcast()
{
    if (_clients.empty())
        return; // No clients, skip polling

    for (auto &[channel, info] : _channels)
    {
        try
        {
            json new_data = info.fetcher();
            if (new_data != info.cached_data)
            {
                info.cached_data = new_data;
                broadcast(channel, new_data.dump());
            }
        }
        catch (const std::exception &e)
        {
            spdlog::warn("[{}:{}]: Failed to poll channel {}: {}",
                         __class__, __func__, channel, e.what());
        }
    }

    // Send keepalive comment to all clients (helps detect dead connections)
    for (auto &client : _clients)
    {
        send_sse_comment(client.req, "keepalive");
    }
}

void sse_manager::on_client_close(evhttp_connection *conn, void *arg)
{
    auto *self = static_cast<sse_manager *>(arg);
    // Find and remove the client with this connection
    self->_clients.erase(
        std::remove_if(self->_clients.begin(), self->_clients.end(),
                       [conn](const SseClient &c)
                       {
                           return c.req &&
                                  evhttp_request_get_connection(c.req) == conn;
                       }),
        self->_clients.end());
    spdlog::info("[{}:{}]: SSE client connection closed, total={}",
                 __class__, __func__, self->_clients.size());
}

void sse_manager::send_sse_event(evhttp_request *req, const std::string &event_name, const std::string &data)
{
    evbuffer *buf = evbuffer_new();
    if (!buf)
        return;

    // SSE format: event: <name>\ndata: <json>\n\n
    evbuffer_add_printf(buf, "event: %s\n", event_name.c_str());

    // Split data by newlines (SSE spec: each line prefixed with "data: ")
    size_t pos = 0;
    while (pos < data.size())
    {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos)
        {
            evbuffer_add_printf(buf, "data: %s\n", data.substr(pos).c_str());
            break;
        }
        else
        {
            evbuffer_add_printf(buf, "data: %s\n", data.substr(pos, nl - pos).c_str());
            pos = nl + 1;
        }
    }
    evbuffer_add_printf(buf, "\n"); // End of event (double newline)

    evhttp_send_reply_chunk(req, buf);
    evbuffer_free(buf);
}

void sse_manager::send_sse_comment(evhttp_request *req, const std::string &comment)
{
    evbuffer *buf = evbuffer_new();
    if (!buf)
        return;

    evbuffer_add_printf(buf, ": %s\n\n", comment.c_str());
    evhttp_send_reply_chunk(req, buf);
    evbuffer_free(buf);
}
