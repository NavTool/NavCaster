#include "http_handler.h"
#include "SysUsage.h"
#include "Caster_Core.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#define __class__ "http_handler"

// Redis key constants — matching CasterWeb and caster_internal
static const char *KEY_ACCOUNT_RECORD = "ACT:RECORD";
static const char *KEY_ACCOUNT_ACTIVE = "STR:ACTIVE";
static const char *KEY_SOURCE_RECORD = "MPT:RECORD";
static const char *KEY_SERVER_STATE = "MPT:STAT";
static const char *KEY_CLIENT_STATE = "USR:STAT";
static const char *KEY_STREAM_STATE = "STR:STAT";
static const char *KEY_ALIAS_RULE = "ALIAS:RULE";
static const char *KEY_ACCESS_GROUP = "ACCESS:GROUP";
static const char *KEY_ACCESS_ITEM = "ACCESS:ITEM"; // + ":group_uid"
static const char *KEY_PULL_RECORD = "PULL:RECORD";
static const char *KEY_PULL_STATE = "PULL:STAT";
static const char *KEY_PUSH_RECORD = "PUSH:RECORD";
static const char *KEY_PUSH_STATE = "PUSH:STAT";
static const char *KEY_CASTER_NODE = "CASTER:NODE";

// PRAGMATIC APPROACH: Since all Redis operations are hash operations on local
// Redis with sub-millisecond latency, and the HTTP API is for management only,
// we use synchronous hiredis calls on a dedicated blocking connection.

namespace
{
    // Synchronous Redis helper using a blocking connection
    class sync_redis
    {
    public:
        static sync_redis &instance()
        {
            static sync_redis inst;
            return inst;
        }

        int init(const std::string &host, int port, const std::string &password)
        {
            _host = host;
            _port = port;
            _password = password;
            return reconnect();
        }

        int reconnect()
        {
            if (_ctx)
            {
                redisFree(_ctx);
                _ctx = nullptr;
            }
            struct timeval tv = {2, 0};
            _ctx = redisConnectWithTimeout(_host.c_str(), _port, tv);
            if (!_ctx || _ctx->err)
            {
                spdlog::error("[sync_redis]: Connect failed: {}", _ctx ? _ctx->errstr : "null");
                if (_ctx)
                {
                    redisFree(_ctx);
                    _ctx = nullptr;
                }
                return -1;
            }
            if (!_password.empty())
            {
                auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "AUTH %s", _password.c_str()));
                if (reply)
                    freeReplyObject(reply);
            }
            return 0;
        }

        // HGETALL → json object {field: parsed_value, ...}
        json hgetall(const char *key)
        {
            if (!ensure_connected())
                return json::object();

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "HGETALL %s", key));
            if (!reply)
            {
                reconnect();
                return json::object();
            }

            json result = json::object();
            if (reply->type == REDIS_REPLY_ARRAY)
            {
                for (size_t i = 0; i + 1 < reply->elements; i += 2)
                {
                    std::string field = reply->element[i]->str ? reply->element[i]->str : "";
                    std::string value = reply->element[i + 1]->str ? reply->element[i + 1]->str : "";
                    try
                    {
                        result[field] = json::parse(value);
                    }
                    catch (...)
                    {
                        result[field] = value;
                    }
                }
            }
            freeReplyObject(reply);
            return result;
        }

        // HGET → json value or null
        json hget(const char *key, const char *field)
        {
            if (!ensure_connected())
                return nullptr;

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "HGET %s %s", key, field));
            if (!reply)
            {
                reconnect();
                return nullptr;
            }

            json result = nullptr;
            if (reply->type == REDIS_REPLY_STRING && reply->str)
            {
                try
                {
                    result = json::parse(reply->str);
                }
                catch (...)
                {
                    result = std::string(reply->str);
                }
            }
            freeReplyObject(reply);
            return result;
        }

        // HSET → bool success
        bool hset(const char *key, const char *field, const std::string &value)
        {
            if (!ensure_connected())
                return false;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "HSET %s %s %s", key, field, value.c_str()));
            bool ok = reply && reply->type != REDIS_REPLY_ERROR;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return ok;
        }

        // HSETNX → bool success (false if already exists)
        bool hsetnx(const char *key, const char *field, const std::string &value)
        {
            if (!ensure_connected())
                return false;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "HSETNX %s %s %s", key, field, value.c_str()));
            bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return ok;
        }

        // HDEL → bool success
        bool hdel(const char *key, const char *field)
        {
            if (!ensure_connected())
                return false;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "HDEL %s %s", key, field));
            bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return ok;
        }

    private:
        bool ensure_connected()
        {
            if (_ctx && !_ctx->err)
                return true;
            return reconnect() == 0;
        }

        redisContext *_ctx = nullptr;
        std::string _host;
        int _port = 6379;
        std::string _password;
    };

    // Second sync redis for auth operations
    class sync_redis_auth
    {
    public:
        static sync_redis_auth &instance()
        {
            static sync_redis_auth inst;
            return inst;
        }

        int init(const std::string &host, int port, const std::string &password)
        {
            return _redis.init(host, port, password);
        }

        sync_redis &redis() { return _redis; }

    private:
        sync_redis _redis;
    };

} // anonymous namespace

http_handler::http_handler() {}
http_handler::~http_handler() {}

int http_handler::init(event_base *base, redis_adapter *caster_redis, redis_adapter *auth_redis, const HttpApiConfig &config)
{
    _caster_redis = caster_redis;
    _auth_redis = auth_redis;
    _config = config;

    // Initialize synchronous Redis connection for blocking API operations
    sync_redis::instance().init(config.redis_host, config.redis_port, config.redis_password);

    // Configure server
    _server.set_cors_origin(config.cors_origin);
    _server.add_public_path("/api/auth/login");
    _server.add_public_path("/api/status/health");
    _server.set_auth_validator([this](const std::string &token) -> bool
                               { return validate_token(token); });

    // ==================== Auth ====================
    _server.route(EVHTTP_REQ_POST, "/api/auth/login", [this](auto &req, auto &resp)
                  { handle_login(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/auth/logout", [this](auto &req, auto &resp)
                  { handle_logout(req, resp); });

    // ==================== Accounts ====================
    _server.route(EVHTTP_REQ_GET, "/api/accounts", [this](auto &req, auto &resp)
                  { handle_get_accounts(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/accounts/*", [this](auto &req, auto &resp)
                  { handle_get_account(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/accounts", [this](auto &req, auto &resp)
                  { handle_create_account(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/accounts/*", [this](auto &req, auto &resp)
                  { handle_update_account(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/accounts/*", [this](auto &req, auto &resp)
                  { handle_delete_account(req, resp); });

    // ==================== Account Actives ====================
    _server.route(EVHTTP_REQ_GET, "/api/accounts/active", [this](auto &req, auto &resp)
                  { handle_get_account_actives(req, resp); });

    // ==================== Sources ====================
    _server.route(EVHTTP_REQ_GET, "/api/sources", [this](auto &req, auto &resp)
                  { handle_get_sources(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/sources/*", [this](auto &req, auto &resp)
                  { handle_get_source(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/sources", [this](auto &req, auto &resp)
                  { handle_create_source(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/sources/*", [this](auto &req, auto &resp)
                  { handle_update_source(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/sources/*", [this](auto &req, auto &resp)
                  { handle_delete_source(req, resp); });

    // ==================== Servers (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/servers", [this](auto &req, auto &resp)
                  { handle_get_servers(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/servers/*", [this](auto &req, auto &resp)
                  { handle_get_server(req, resp); });

    // ==================== Clients (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/clients", [this](auto &req, auto &resp)
                  { handle_get_clients(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/clients/*", [this](auto &req, auto &resp)
                  { handle_get_client(req, resp); });

    // ==================== Streams (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/streams", [this](auto &req, auto &resp)
                  { handle_get_streams(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/streams/*", [this](auto &req, auto &resp)
                  { handle_get_stream(req, resp); });

    // ==================== Aliases ====================
    _server.route(EVHTTP_REQ_GET, "/api/aliases", [this](auto &req, auto &resp)
                  { handle_get_aliases(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/aliases/*", [this](auto &req, auto &resp)
                  { handle_get_alias(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/aliases", [this](auto &req, auto &resp)
                  { handle_create_alias(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/aliases/*", [this](auto &req, auto &resp)
                  { handle_update_alias(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/aliases/*", [this](auto &req, auto &resp)
                  { handle_delete_alias(req, resp); });

    // ==================== Access Groups ====================
    _server.route(EVHTTP_REQ_GET, "/api/access/groups", [this](auto &req, auto &resp)
                  { handle_get_access_groups(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/access/groups/*", [this](auto &req, auto &resp)
                  { handle_get_access_group(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/access/groups", [this](auto &req, auto &resp)
                  { handle_create_access_group(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/access/groups/*", [this](auto &req, auto &resp)
                  { handle_update_access_group(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/access/groups/*", [this](auto &req, auto &resp)
                  { handle_delete_access_group(req, resp); });

    // ==================== Access Items ====================
    _server.route(EVHTTP_REQ_GET, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_get_access_items(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_create_access_item(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_update_access_item(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_delete_access_item(req, resp); });

    // ==================== Pull Relays ====================
    _server.route(EVHTTP_REQ_GET, "/api/relays/pull", [this](auto &req, auto &resp)
                  { handle_get_pulls(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/pull/status", [this](auto &req, auto &resp)
                  { handle_get_pull_states(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/pull/*", [this](auto &req, auto &resp)
                  { handle_get_pull(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/pull", [this](auto &req, auto &resp)
                  { handle_create_pull(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/relays/pull/*", [this](auto &req, auto &resp)
                  { handle_update_pull(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/relays/pull/*", [this](auto &req, auto &resp)
                  { handle_delete_pull(req, resp); });

    // ==================== Push Relays ====================
    _server.route(EVHTTP_REQ_GET, "/api/relays/push", [this](auto &req, auto &resp)
                  { handle_get_pushs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/push/status", [this](auto &req, auto &resp)
                  { handle_get_push_states(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/push/*", [this](auto &req, auto &resp)
                  { handle_get_push(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/push", [this](auto &req, auto &resp)
                  { handle_create_push(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/relays/push/*", [this](auto &req, auto &resp)
                  { handle_update_push(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/relays/push/*", [this](auto &req, auto &resp)
                  { handle_delete_push(req, resp); });

    // ==================== Nodes (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/nodes", [this](auto &req, auto &resp)
                  { handle_get_nodes(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/nodes/*", [this](auto &req, auto &resp)
                  { handle_get_node(req, resp); });

    // ==================== Status ====================
    _server.route(EVHTTP_REQ_GET, "/api/status", [this](auto &req, auto &resp)
                  { handle_get_status(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/status/health", [this](auto &req, auto &resp)
                  { handle_get_health(req, resp); });

    // ==================== Static File Serving ====================
    if (!config.web_root.empty())
    {
        _server.set_static_root(config.web_root);
    }

    // ==================== SSE (Server-Sent Events) ====================
    _server.route_raw(EVHTTP_REQ_GET, "/api/events/stream",
                      [this](evhttp_request *raw_req, const HttpRequest &req)
                      { handle_sse_stream(raw_req, req); });
    _server.add_public_path("/api/events/stream"); // Auth via query param token

    // Start server
    int ret = _server.init(base, config.port, config.bind_addr);
    if (ret != 0)
    {
        spdlog::error("[{}:{}]: Failed to start HTTP API server", __class__, __func__);
        return ret;
    }

    // Initialize SSE manager with data channels
    _sse.init(base, 2); // 2-second update interval

    // Register SSE channels — each maps to a Redis HGETALL key
    auto &redis = sync_redis::instance();
    _sse.register_channel("servers", [&redis]() -> json
                          { return redis.hgetall(KEY_SERVER_STATE); });
    _sse.register_channel("clients", [&redis]() -> json
                          { return redis.hgetall(KEY_CLIENT_STATE); });
    _sse.register_channel("streams", [&redis]() -> json
                          { return redis.hgetall(KEY_STREAM_STATE); });
    _sse.register_channel("nodes", [&redis]() -> json
                          { return redis.hgetall(KEY_CASTER_NODE); });
    _sse.register_channel("accounts", [&redis]() -> json
                          { return redis.hgetall(KEY_ACCOUNT_RECORD); });
    _sse.register_channel("sources", [&redis]() -> json
                          { return redis.hgetall(KEY_SOURCE_RECORD); });
    _sse.register_channel("aliases", [&redis]() -> json
                          { return redis.hgetall(KEY_ALIAS_RULE); });
    _sse.register_channel("access_groups", [&redis]() -> json
                          { return redis.hgetall(KEY_ACCESS_GROUP); });
    _sse.register_channel("pull_records", [&redis]() -> json
                          { return redis.hgetall(KEY_PULL_RECORD); });
    _sse.register_channel("pull_states", [&redis]() -> json
                          { return redis.hgetall(KEY_PULL_STATE); });
    _sse.register_channel("push_records", [&redis]() -> json
                          { return redis.hgetall(KEY_PUSH_RECORD); });
    _sse.register_channel("push_states", [&redis]() -> json
                          { return redis.hgetall(KEY_PUSH_STATE); });
    _sse.register_channel("account_actives", [&redis]() -> json
                          { return redis.hgetall(KEY_ACCOUNT_ACTIVE); });

    spdlog::info("[{}:{}]: HTTP API handler initialized on port {}", __class__, __func__, config.port);
    return 0;
}

// ==================== Helpers ====================

std::string http_handler::get_resource_id(const HttpRequest &req) const
{
    // Return last path segment
    if (!req.path_segments.empty())
        return req.path_segments.back();
    return {};
}

std::string http_handler::get_path_segment(const HttpRequest &req, size_t index) const
{
    if (index < req.path_segments.size())
        return req.path_segments[index];
    return {};
}

std::string http_handler::generate_token()
{
    static const char charset[] = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string token;
    token.reserve(64);
    for (int i = 0; i < 64; ++i)
        token += charset[dist(gen)];

    std::lock_guard<std::mutex> lock(_token_mutex);
    _active_tokens.insert(token);
    return token;
}

bool http_handler::validate_token(const std::string &token)
{
    if (token.empty())
        return false;
    std::lock_guard<std::mutex> lock(_token_mutex);
    return _active_tokens.count(token) > 0;
}

void http_handler::invalidate_token(const std::string &token)
{
    std::lock_guard<std::mutex> lock(_token_mutex);
    _active_tokens.erase(token);
}

// ==================== Auth ====================

void http_handler::handle_login(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Invalid JSON body"})";
        return;
    }

    std::string user = body.value("username", "");
    std::string pass = body.value("password", "");

    if (user == _config.admin_user && pass == _config.admin_password)
    {
        std::string token = generate_token();
        json result = {{"token", token}, {"username", user}};
        resp.status_code = 200;
        resp.body = result.dump();
    }
    else
    {
        resp.status_code = 401;
        resp.body = R"({"error":"Invalid credentials"})";
    }
}

void http_handler::handle_logout(const HttpRequest &req, HttpResponse &resp)
{
    auto it = req.headers.find("Authorization");
    if (it != req.headers.end() && it->second.size() > 7)
    {
        std::string token = it->second.substr(7);
        invalidate_token(token);
    }
    resp.status_code = 200;
    resp.body = R"({"ok":true})";
}

// ==================== Generic CRUD pattern ====================

// Macro for common CRUD pattern — reduces boilerplate
#define IMPL_GET_ALL(handler_name, redis_key)                          \
    void http_handler::handler_name(const HttpRequest &req, HttpResponse &resp) \
    {                                                                  \
        json data = sync_redis::instance().hgetall(redis_key);         \
        resp.status_code = 200;                                        \
        resp.body = data.dump();                                       \
    }

#define IMPL_GET_ONE(handler_name, redis_key)                          \
    void http_handler::handler_name(const HttpRequest &req, HttpResponse &resp) \
    {                                                                  \
        std::string id = get_resource_id(req);                         \
        if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; } \
        json data = sync_redis::instance().hget(redis_key, id.c_str()); \
        if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; } \
        resp.status_code = 200;                                        \
        resp.body = data.dump();                                       \
    }

#define IMPL_CREATE(handler_name, redis_key, field_name)               \
    void http_handler::handler_name(const HttpRequest &req, HttpResponse &resp) \
    {                                                                  \
        json body;                                                     \
        try { body = json::parse(req.body); }                          \
        catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; } \
        std::string field = body.value(field_name, "");                \
        if (field.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing )" field_name R"("})"; return; } \
        bool ok = sync_redis::instance().hsetnx(redis_key, field.c_str(), body.dump()); \
        if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Already exists"})"; return; } \
        resp.status_code = 201;                                        \
        resp.body = json{{"ok", true}, {"field", field}}.dump();       \
    }

#define IMPL_UPDATE(handler_name, redis_key)                           \
    void http_handler::handler_name(const HttpRequest &req, HttpResponse &resp) \
    {                                                                  \
        std::string id = get_resource_id(req);                         \
        if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; } \
        json body;                                                     \
        try { body = json::parse(req.body); }                          \
        catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; } \
        bool ok = sync_redis::instance().hset(redis_key, id.c_str(), body.dump()); \
        if (!ok) { resp.status_code = 500; resp.body = R"({"error":"Redis error"})"; return; } \
        resp.status_code = 200;                                        \
        resp.body = json{{"ok", true}}.dump();                         \
    }

#define IMPL_DELETE(handler_name, redis_key)                           \
    void http_handler::handler_name(const HttpRequest &req, HttpResponse &resp) \
    {                                                                  \
        std::string id = get_resource_id(req);                         \
        if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; } \
        bool ok = sync_redis::instance().hdel(redis_key, id.c_str()); \
        if (!ok) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; } \
        resp.status_code = 200;                                        \
        resp.body = json{{"ok", true}}.dump();                         \
    }

// ==================== Accounts (ACT:RECORD) — uses auth redis ====================

void http_handler::handle_get_accounts(const HttpRequest &req, HttpResponse &resp)
{
    json data = sync_redis_auth::instance().redis().hgetall(KEY_ACCOUNT_RECORD);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_account(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty() || id == "active")
    {
        handle_get_account_actives(req, resp);
        return;
    }
    json data = sync_redis_auth::instance().redis().hget(KEY_ACCOUNT_RECORD, id.c_str());
    if (data.is_null())
    {
        resp.status_code = 404;
        resp.body = R"({"error":"Account not found"})";
        return;
    }
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_create_account(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Invalid JSON"})";
        return;
    }
    std::string account = body.value("account", "");
    if (account.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing account field"})";
        return;
    }
    bool ok = sync_redis_auth::instance().redis().hsetnx(KEY_ACCOUNT_RECORD, account.c_str(), body.dump());
    if (!ok)
    {
        resp.status_code = 409;
        resp.body = R"({"error":"Account already exists"})";
        return;
    }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"account", account}}.dump();
}

void http_handler::handle_update_account(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing account name"})";
        return;
    }
    json body;
    try
    {
        body = json::parse(req.body);
    }
    catch (...)
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Invalid JSON"})";
        return;
    }
    bool ok = sync_redis_auth::instance().redis().hset(KEY_ACCOUNT_RECORD, id.c_str(), body.dump());
    if (!ok)
    {
        resp.status_code = 500;
        resp.body = R"({"error":"Redis error"})";
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_account(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing account name"})";
        return;
    }
    bool ok = sync_redis_auth::instance().redis().hdel(KEY_ACCOUNT_RECORD, id.c_str());
    if (!ok)
    {
        resp.status_code = 404;
        resp.body = R"({"error":"Account not found"})";
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_get_account_actives(const HttpRequest &req, HttpResponse &resp)
{
    json data = sync_redis_auth::instance().redis().hgetall(KEY_ACCOUNT_ACTIVE);
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Sources (MPT:RECORD) ====================

IMPL_GET_ALL(handle_get_sources, KEY_SOURCE_RECORD)
IMPL_GET_ONE(handle_get_source, KEY_SOURCE_RECORD)

void http_handler::handle_create_source(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    std::string mountpoint = body.value("mountpoint", "");
    if (mountpoint.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing mountpoint"})"; return; }
    bool ok = sync_redis::instance().hsetnx(KEY_SOURCE_RECORD, mountpoint.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Source already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"mountpoint", mountpoint}}.dump();
}

IMPL_UPDATE(handle_update_source, KEY_SOURCE_RECORD)
IMPL_DELETE(handle_delete_source, KEY_SOURCE_RECORD)

// ==================== Servers (MPT:STAT) read-only ====================

IMPL_GET_ALL(handle_get_servers, KEY_SERVER_STATE)
IMPL_GET_ONE(handle_get_server, KEY_SERVER_STATE)

// ==================== Clients (USR:STAT) read-only ====================

IMPL_GET_ALL(handle_get_clients, KEY_CLIENT_STATE)
IMPL_GET_ONE(handle_get_client, KEY_CLIENT_STATE)

// ==================== Streams (STR:STAT) read-only ====================

IMPL_GET_ALL(handle_get_streams, KEY_STREAM_STATE)
IMPL_GET_ONE(handle_get_stream, KEY_STREAM_STATE)

// ==================== Aliases (ALIAS:RULE) ====================

IMPL_GET_ALL(handle_get_aliases, KEY_ALIAS_RULE)
IMPL_GET_ONE(handle_get_alias, KEY_ALIAS_RULE)

void http_handler::handle_create_alias(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    std::string name = body.value("alias_mpt", "");
    if (name.empty()) name = body.value("name", "");
    if (name.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing alias name"})"; return; }
    bool ok = sync_redis::instance().hsetnx(KEY_ALIAS_RULE, name.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Alias already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"alias", name}}.dump();
}

IMPL_UPDATE(handle_update_alias, KEY_ALIAS_RULE)
IMPL_DELETE(handle_delete_alias, KEY_ALIAS_RULE)

// ==================== Access Groups (ACCESS:GROUP) ====================

IMPL_GET_ALL(handle_get_access_groups, KEY_ACCESS_GROUP)
IMPL_GET_ONE(handle_get_access_group, KEY_ACCESS_GROUP)

void http_handler::handle_create_access_group(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    std::string uid = body.value("uid", "");
    if (uid.empty()) uid = body.value("group_uid", "");
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing group uid"})"; return; }
    bool ok = sync_redis::instance().hsetnx(KEY_ACCESS_GROUP, uid.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Group already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

IMPL_UPDATE(handle_update_access_group, KEY_ACCESS_GROUP)
IMPL_DELETE(handle_delete_access_group, KEY_ACCESS_GROUP)

// ==================== Access Items (ACCESS:ITEM:<group_uid>) ====================

void http_handler::handle_get_access_items(const HttpRequest &req, HttpResponse &resp)
{
    // Path: /api/access/items/<group_uid>
    std::string group_uid = get_resource_id(req);
    if (group_uid.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing group_uid"})";
        return;
    }
    std::string key = std::string(KEY_ACCESS_ITEM) + ":" + group_uid;
    json data = sync_redis::instance().hgetall(key.c_str());
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_create_access_item(const HttpRequest &req, HttpResponse &resp)
{
    std::string group_uid = get_resource_id(req);
    if (group_uid.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing group_uid"})";
        return;
    }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }

    std::string mount = body.value("mountpoint", "");
    if (mount.empty()) mount = body.value("mount", "");
    if (mount.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing mountpoint"})"; return; }

    std::string key = std::string(KEY_ACCESS_ITEM) + ":" + group_uid;
    bool ok = sync_redis::instance().hsetnx(key.c_str(), mount.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Item already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_update_access_item(const HttpRequest &req, HttpResponse &resp)
{
    // For update, expect body to contain "group_uid" and "mountpoint"
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }

    std::string group_uid = body.value("group_uid", get_resource_id(req));
    std::string mount = body.value("mountpoint", "");
    if (mount.empty()) mount = body.value("mount", "");
    if (group_uid.empty() || mount.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing group_uid or mountpoint"})";
        return;
    }

    std::string key = std::string(KEY_ACCESS_ITEM) + ":" + group_uid;
    bool ok = sync_redis::instance().hset(key.c_str(), mount.c_str(), body.dump());
    if (!ok) { resp.status_code = 500; resp.body = R"({"error":"Redis error"})"; return; }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_access_item(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON, need group_uid and mountpoint"})"; return; }

    std::string group_uid = body.value("group_uid", get_resource_id(req));
    std::string mount = body.value("mountpoint", "");
    if (mount.empty()) mount = body.value("mount", "");
    if (group_uid.empty() || mount.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing group_uid or mountpoint"})";
        return;
    }

    std::string key = std::string(KEY_ACCESS_ITEM) + ":" + group_uid;
    bool ok = sync_redis::instance().hdel(key.c_str(), mount.c_str());
    if (!ok) { resp.status_code = 404; resp.body = R"({"error":"Item not found"})"; return; }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

// ==================== Pull Relays (PULL:RECORD / PULL:STAT) ====================

IMPL_GET_ALL(handle_get_pulls, KEY_PULL_RECORD)
IMPL_GET_ONE(handle_get_pull, KEY_PULL_RECORD)

void http_handler::handle_create_pull(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    std::string uid = body.value("uid", "");
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing uid"})"; return; }
    bool ok = sync_redis::instance().hsetnx(KEY_PULL_RECORD, uid.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Pull record already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

IMPL_UPDATE(handle_update_pull, KEY_PULL_RECORD)
IMPL_DELETE(handle_delete_pull, KEY_PULL_RECORD)
IMPL_GET_ALL(handle_get_pull_states, KEY_PULL_STATE)

// ==================== Push Relays (PUSH:RECORD / PUSH:STAT) ====================

IMPL_GET_ALL(handle_get_pushs, KEY_PUSH_RECORD)
IMPL_GET_ONE(handle_get_push, KEY_PUSH_RECORD)

void http_handler::handle_create_push(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    std::string uid = body.value("uid", "");
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing uid"})"; return; }
    bool ok = sync_redis::instance().hsetnx(KEY_PUSH_RECORD, uid.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Push record already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

IMPL_UPDATE(handle_update_push, KEY_PUSH_RECORD)
IMPL_DELETE(handle_delete_push, KEY_PUSH_RECORD)
IMPL_GET_ALL(handle_get_push_states, KEY_PUSH_STATE)

// ==================== Nodes (CASTER:NODE) read-only ====================

IMPL_GET_ALL(handle_get_nodes, KEY_CASTER_NODE)
IMPL_GET_ONE(handle_get_node, KEY_CASTER_NODE)

// ==================== Status ====================

void http_handler::handle_get_status(const HttpRequest &req, HttpResponse &resp)
{
    json status;

    // System info
    double cpu = SysUsage::getInstance()->getProcessCPU();
    size_t mem = SysUsage::getInstance()->getProcessMemory();
    status["cpu_percent"] = cpu;
    status["memory_bytes"] = mem;
    status["memory_mb"] = mem / 1024.0 / 1024.0;

    // Caster core status
    std::string caster_status = CASTER::Get_Status();
    try
    {
        status["caster"] = json::parse(caster_status);
    }
    catch (...)
    {
        status["caster"] = caster_status;
    }

    // Redis connectivity
    status["redis_caster_connected"] = _caster_redis ? _caster_redis->is_connected() : false;
    status["redis_auth_connected"] = _auth_redis ? _auth_redis->is_connected() : false;

    resp.status_code = 200;
    resp.body = status.dump();
}

void http_handler::handle_get_health(const HttpRequest &req, HttpResponse &resp)
{
    resp.status_code = 200;
    resp.body = R"({"status":"ok"})";
}

// ==================== SSE ====================

void http_handler::handle_sse_stream(evhttp_request *raw_req, const HttpRequest &req)
{
    // Auth via query parameter: ?token=xxx
    auto it = req.query_params.find("token");
    if (it == req.query_params.end() || !validate_token(it->second))
    {
        // Also check Authorization header (already parsed in req.headers)
        auto auth_it = req.headers.find("Authorization");
        bool authed = false;
        if (auth_it != req.headers.end() && auth_it->second.size() > 7)
        {
            std::string token = auth_it->second.substr(7);
            if (validate_token(token))
                authed = true;
        }
        if (!authed)
        {
            evhttp_send_error(raw_req, 401, "Unauthorized");
            return;
        }
    }

    // Get optional channel filter from query params
    auto ch_it = req.query_params.find("channels");
    std::string channels = (ch_it != req.query_params.end()) ? ch_it->second : "*";

    _sse.add_client(raw_req, channels);
}

// Cleanup macros
#undef IMPL_GET_ALL
#undef IMPL_GET_ONE
#undef IMPL_CREATE
#undef IMPL_UPDATE
#undef IMPL_DELETE
