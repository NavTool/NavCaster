#include "redis_adapter.h"
#include <spdlog/spdlog.h>

#define __class__ "redis_adapter"

redis_adapter::redis_adapter() {}

redis_adapter::~redis_adapter()
{
    _shutting_down = true;
    if (_reconnect_event)
    {
        event_del(_reconnect_event);
        event_free(_reconnect_event);
        _reconnect_event = nullptr;
    }
    if (_ctx)
    {
        redisAsyncDisconnect(_ctx);
        _ctx = nullptr;
    }
}

int redis_adapter::init(event_base *base, const std::string &host, int port, const std::string &password)
{
    _base = base;
    _host = host;
    _port = port;
    _password = password;

    const int result = connect_async();
    if (result != 0)
    {
        schedule_reconnect();
    }
    return result;
}

int redis_adapter::connect_async()
{
    if (_shutting_down)
    {
        return -1;
    }
    if (_ctx)
    {
        redisAsyncDisconnect(_ctx);
        _ctx = nullptr;
    }

    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, _host.c_str(), _port);
    struct timeval tv = {0};
    tv.tv_sec = 10;
    options.connect_timeout = &tv;

    _ctx = redisAsyncConnectWithOptions(&options);
    if (!_ctx)
    {
        spdlog::error("[{}:{}]: Failed to create Redis async context", __class__, __func__);
        return -1;
    }
    if (_ctx->err)
    {
        spdlog::error("[{}:{}]: Redis connect error: {}", __class__, __func__, _ctx->errstr);
        redisAsyncFree(_ctx);
        _ctx = nullptr;
        return -1;
    }

    _ctx->data = this;
    redisLibeventAttach(_ctx, _base);
    redisAsyncSetConnectCallback(_ctx, connect_callback);
    redisAsyncSetDisconnectCallback(_ctx, disconnect_callback);

    // Authenticate if password is provided
    if (!_password.empty())
    {
        redisAsyncCommand(_ctx, nullptr, nullptr, "AUTH %s", _password.c_str());
    }

    return 0;
}

void redis_adapter::connect_callback(const redisAsyncContext *c, int status)
{
    auto *adapter = static_cast<redis_adapter *>(c->data);
    if (!adapter)
    {
        return;
    }
    if (status != REDIS_OK)
    {
        spdlog::error("[{}]: Redis connect failed: {}", __class__, c->errstr);
        adapter->_connected = false;
        adapter->_ctx = nullptr;
        adapter->schedule_reconnect();
        return;
    }
    spdlog::info("[{}]: Redis connected for HTTP API", __class__);
    adapter->_connected = true;
    adapter->_reconnect_attempts = 0;
    adapter->execute_pending();
}

void redis_adapter::disconnect_callback(const redisAsyncContext *c, int status)
{
    auto *adapter = static_cast<redis_adapter *>(c->data);
    if (!adapter)
    {
        return;
    }
    adapter->_connected = false;
    adapter->_ctx = nullptr;
    if (status != REDIS_OK)
    {
        spdlog::warn("[{}]: Redis disconnected with error: {}", __class__, c->errstr);
        adapter->schedule_reconnect();
    }
    else
    {
        spdlog::info("[{}]: Redis disconnected", __class__);
    }
}

void redis_adapter::reconnect_callback(evutil_socket_t /*fd*/, short /*what*/, void *arg)
{
    auto *adapter = static_cast<redis_adapter *>(arg);
    if (!adapter || adapter->_shutting_down)
    {
        return;
    }

    adapter->_reconnect_scheduled = false;
    spdlog::info("[{}]: attempting Redis reconnect for HTTP API", __class__);
    if (adapter->connect_async() != 0)
    {
        adapter->schedule_reconnect();
    }
}

void redis_adapter::schedule_reconnect()
{
    if (_shutting_down || !_base || _connected || _reconnect_scheduled)
    {
        return;
    }

    if (!_reconnect_event)
    {
        _reconnect_event = event_new(_base, -1, 0, reconnect_callback, this);
        if (!_reconnect_event)
        {
            spdlog::error("[{}]: failed to create Redis reconnect timer", __class__);
            return;
        }
    }

    _reconnect_attempts = std::min(_reconnect_attempts + 1, 5);
    const int delay_sec = std::min(_reconnect_attempts == 1 ? 1 : _reconnect_attempts * 2, 10);
    timeval tv{delay_sec, 0};
    if (event_add(_reconnect_event, &tv) != 0)
    {
        spdlog::error("[{}]: failed to schedule Redis reconnect timer", __class__);
        return;
    }
    _reconnect_scheduled = true;
    spdlog::warn("[{}]: scheduled Redis reconnect in {}s", __class__, delay_sec);
}

void redis_adapter::execute_pending()
{
    while (!_pending.empty())
    {
        auto &cmd = _pending.front();
        auto *ctx = new RedisRequestContext{cmd.callback, "PENDING", ""};
        redisAsyncCommand(_ctx, cmd.redis_cb, ctx, cmd.cmd.c_str());
        _pending.pop();
    }
}

// ==================== HGETALL ====================

void redis_adapter::hash_get_all(const std::string &key, RedisResultCallback cb)
{
    auto *ctx = new RedisRequestContext{cb, "HGETALL", key};
    if (!_ctx || !_connected)
    {
        json err = {{"error", "Redis not connected"}};
        cb(false, err);
        delete ctx;
        return;
    }
    redisAsyncCommand(_ctx, hgetall_callback, ctx, "HGETALL %s", key.c_str());
}

void redis_adapter::hgetall_callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto *ctx = static_cast<RedisRequestContext *>(privdata);
    auto *reply = static_cast<redisReply *>(r);

    if (!reply)
    {
        ctx->callback(false, {{"error", "No reply"}});
        delete ctx;
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        ctx->callback(true, json::object());
        delete ctx;
        return;
    }

    if (reply->type != REDIS_REPLY_ARRAY)
    {
        ctx->callback(false, {{"error", "Unexpected reply type"}, {"type", reply->type}});
        delete ctx;
        return;
    }

    // HGETALL returns [field1, value1, field2, value2, ...]
    json result = json::object();
    for (size_t i = 0; i + 1 < reply->elements; i += 2)
    {
        std::string field = reply->element[i]->str ? reply->element[i]->str : "";
        std::string value = reply->element[i + 1]->str ? reply->element[i + 1]->str : "";

        // Try to parse value as JSON, fallback to string
        try
        {
            result[field] = json::parse(value);
        }
        catch (...)
        {
            result[field] = value;
        }
    }

    ctx->callback(true, result);
    delete ctx;
}

// ==================== HGET ====================

void redis_adapter::hash_get(const std::string &key, const std::string &field, RedisResultCallback cb)
{
    auto *ctx = new RedisRequestContext{cb, "HGET", key};
    if (!_ctx || !_connected)
    {
        cb(false, {{"error", "Redis not connected"}});
        delete ctx;
        return;
    }
    redisAsyncCommand(_ctx, hget_callback, ctx, "HGET %s %s", key.c_str(), field.c_str());
}

void redis_adapter::hget_callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto *ctx = static_cast<RedisRequestContext *>(privdata);
    auto *reply = static_cast<redisReply *>(r);

    if (!reply)
    {
        ctx->callback(false, {{"error", "No reply"}});
        delete ctx;
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        ctx->callback(false, {{"error", "Not found"}});
        delete ctx;
        return;
    }

    if (reply->type != REDIS_REPLY_STRING)
    {
        ctx->callback(false, {{"error", "Unexpected reply type"}});
        delete ctx;
        return;
    }

    std::string value = reply->str ? reply->str : "";
    try
    {
        ctx->callback(true, json::parse(value));
    }
    catch (...)
    {
        ctx->callback(true, value);
    }
    delete ctx;
}

// ==================== HSET ====================

void redis_adapter::hash_set(const std::string &key, const std::string &field, const std::string &value, RedisResultCallback cb)
{
    auto *ctx = new RedisRequestContext{cb, "HSET", key};
    if (!_ctx || !_connected)
    {
        cb(false, {{"error", "Redis not connected"}});
        delete ctx;
        return;
    }
    redisAsyncCommand(_ctx, hset_callback, ctx, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
}

void redis_adapter::hash_set_nx(const std::string &key, const std::string &field, const std::string &value, RedisResultCallback cb)
{
    auto *ctx = new RedisRequestContext{cb, "HSETNX", key};
    if (!_ctx || !_connected)
    {
        cb(false, {{"error", "Redis not connected"}});
        delete ctx;
        return;
    }
    redisAsyncCommand(_ctx, hset_callback, ctx, "HSETNX %s %s %s", key.c_str(), field.c_str(), value.c_str());
}

void redis_adapter::hset_callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto *ctx = static_cast<RedisRequestContext *>(privdata);
    auto *reply = static_cast<redisReply *>(r);

    if (!reply)
    {
        ctx->callback(false, {{"error", "No reply"}});
        delete ctx;
        return;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        ctx->callback(false, {{"error", reply->str ? reply->str : "Unknown error"}});
        delete ctx;
        return;
    }

    // HSETNX returns 0 if key already exists
    if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0 && ctx->operation == "HSETNX")
    {
        ctx->callback(false, {{"error", "Field already exists"}});
        delete ctx;
        return;
    }

    ctx->callback(true, {{"ok", true}});
    delete ctx;
}

// ==================== HDEL ====================

void redis_adapter::hash_del(const std::string &key, const std::string &field, RedisResultCallback cb)
{
    auto *ctx = new RedisRequestContext{cb, "HDEL", key};
    if (!_ctx || !_connected)
    {
        cb(false, {{"error", "Redis not connected"}});
        delete ctx;
        return;
    }
    redisAsyncCommand(_ctx, hdel_callback, ctx, "HDEL %s %s", key.c_str(), field.c_str());
}

void redis_adapter::hdel_callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto *ctx = static_cast<RedisRequestContext *>(privdata);
    auto *reply = static_cast<redisReply *>(r);

    if (!reply)
    {
        ctx->callback(false, {{"error", "No reply"}});
        delete ctx;
        return;
    }

    if (reply->type == REDIS_REPLY_ERROR)
    {
        ctx->callback(false, {{"error", reply->str ? reply->str : "Unknown error"}});
        delete ctx;
        return;
    }

    if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0)
    {
        ctx->callback(false, {{"error", "Field not found"}});
        delete ctx;
        return;
    }

    ctx->callback(true, {{"ok", true}, {"deleted", 1}});
    delete ctx;
}

// ==================== Generic command ====================

void redis_adapter::command(const std::string &cmd, RedisResultCallback cb)
{
    auto *ctx = new RedisRequestContext{cb, "CMD", ""};
    if (!_ctx || !_connected)
    {
        cb(false, {{"error", "Redis not connected"}});
        delete ctx;
        return;
    }
    redisAsyncCommand(_ctx, generic_callback, ctx, cmd.c_str());
}

void redis_adapter::generic_callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto *ctx = static_cast<RedisRequestContext *>(privdata);
    auto *reply = static_cast<redisReply *>(r);

    if (!reply)
    {
        ctx->callback(false, {{"error", "No reply"}});
        delete ctx;
        return;
    }

    json result;
    switch (reply->type)
    {
    case REDIS_REPLY_STRING:
        result = reply->str ? std::string(reply->str) : "";
        break;
    case REDIS_REPLY_INTEGER:
        result = reply->integer;
        break;
    case REDIS_REPLY_NIL:
        result = nullptr;
        break;
    case REDIS_REPLY_ERROR:
        ctx->callback(false, {{"error", reply->str ? reply->str : "Unknown error"}});
        delete ctx;
        return;
    default:
        result = {{"type", reply->type}};
        break;
    }

    ctx->callback(true, result);
    delete ctx;
}
