#include "http_handler.h"
#include "SysUsage.h"
#include "Caster_Core.h"
#include "base64.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

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
static const char *KEY_CONF_SERVICE = "CONF:SERVICE";
static const char *KEY_CONF_CORE = "CONF:CORE";
static const char *KEY_CONF_AUTH = "CONF:AUTH";
static const char *KEY_MPT_ONLINE = "MPT:LIST";
static const char *KEY_MPT_SUB = "MPT:SUB";

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

        // HLEN → number of fields in hash
        long long hlen(const char *key)
        {
            if (!ensure_connected())
                return 0;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "HLEN %s", key));
            long long count = 0;
            if (reply && reply->type == REDIS_REPLY_INTEGER)
                count = reply->integer;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return count;
        }

        // SET key value (plain string)
        bool set(const char *key, const std::string &value)
        {
            if (!ensure_connected())
                return false;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "SET %s %s", key, value.c_str()));
            bool ok = reply && reply->type != REDIS_REPLY_ERROR;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return ok;
        }

        // GET key → json or null
        json get(const char *key)
        {
            if (!ensure_connected())
                return nullptr;

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "GET %s", key));
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

        // PUBLISH channel message
        bool publish(const char *channel, const std::string &message)
        {
            if (!ensure_connected())
                return false;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "PUBLISH %s %s", channel, message.c_str()));
            bool ok = reply && reply->type != REDIS_REPLY_ERROR;
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

    // Initialize synchronous Redis connections for blocking API operations
    sync_redis::instance().init(config.redis_host, config.redis_port, config.redis_password);
    sync_redis_auth::instance().init(config.auth_redis_host, config.auth_redis_port, config.auth_redis_password);

    // Ensure default access group exists
    {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        json default_group = {
            {"uid", "default"},
            {"group_name", "default"},
            {"create_time", ms},
            {"update_time", ms},
            {"nearest_mpt_enable", false},
            {"nearest_mpt_source_name", ""},
            {"allow_visible_inside_group", true},
            {"allow_access_inside_group", true},
            {"allow_nearby_inside_group", true},
            {"allow_visible_outside_group", true},
            {"allow_access_outside_group", true},
            {"allow_nearby_outside_group", true}
        };
        sync_redis::instance().hsetnx(KEY_ACCESS_GROUP, "default", default_group.dump());
        spdlog::info("[{}:{}]: Ensured default access group exists", __class__, __func__);
    }

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

    // ==================== Relay Start/Stop ====================
    _server.route(EVHTTP_REQ_POST, "/api/relays/pull/start/*", [this](auto &req, auto &resp)
                  { handle_relay_start(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/pull/stop/*", [this](auto &req, auto &resp)
                  { handle_relay_stop(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/push/start/*", [this](auto &req, auto &resp)
                  { handle_relay_start(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/push/stop/*", [this](auto &req, auto &resp)
                  { handle_relay_stop(req, resp); });

    // ==================== Nodes (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/nodes", [this](auto &req, auto &resp)
                  { handle_get_nodes(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/nodes/*", [this](auto &req, auto &resp)
                  { handle_get_node(req, resp); });

    // ==================== Mountpoints ====================
    _server.route(EVHTTP_REQ_GET, "/api/mountpoints/subscribers", [this](auto &req, auto &resp)
                  { handle_get_mountpoint_subscribers(req, resp); });

    // ==================== Status ====================
    _server.route(EVHTTP_REQ_GET, "/api/status", [this](auto &req, auto &resp)
                  { handle_get_status(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/status/health", [this](auto &req, auto &resp)
                  { handle_get_health(req, resp); });

    // ==================== Configuration ====================
    _server.route(EVHTTP_REQ_GET, "/api/config", [this](auto &req, auto &resp)
                  { handle_get_configs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/config/*", [this](auto &req, auto &resp)
                  { handle_get_config(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/config/*", [this](auto &req, auto &resp)
                  { handle_update_config(req, resp); });

    // ==================== Utilities ====================
    _server.route(EVHTTP_REQ_POST, "/api/utils/sourcetable", [this](auto &req, auto &resp)
                  { handle_fetch_sourcetable(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/utils/sourcetable/local", [this](auto &req, auto &resp)
                  { handle_local_sourcetable(req, resp); });

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
        // Also check Redis-stored auth config (allows password change at runtime)
        json auth_conf = sync_redis::instance().get(KEY_CONF_AUTH);
        std::string redis_user = auth_conf.is_object() ? auth_conf.value("admin_user", "") : "";
        std::string redis_pass = auth_conf.is_object() ? auth_conf.value("admin_password", "") : "";
        if (!redis_user.empty() && user == redis_user && pass == redis_pass)
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
    std::string uid = body.value("uid", "");
    if (uid.empty())
    {
        // Fallback: try alias_name or alias_mpt or name
        std::string alias = body.value("alias_name", "");
        if (alias.empty()) alias = body.value("alias_mpt", "");
        if (alias.empty()) alias = body.value("name", "");
        if (alias.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing alias uid"})"; return; }
        uid = alias;
    }
    bool ok = sync_redis::instance().hsetnx(KEY_ALIAS_RULE, uid.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Alias already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"alias", uid}}.dump();
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
    if (!body.contains("enabled")) body["enabled"] = true;
    bool ok = sync_redis::instance().hsetnx(KEY_PULL_RECORD, uid.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Pull record already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

void http_handler::handle_update_pull(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    bool ok = sync_redis::instance().hset(KEY_PULL_RECORD, id.c_str(), body.dump());
    if (!ok) { resp.status_code = 500; resp.body = R"({"error":"Redis error"})"; return; }
    // Delete status to force task restart with new parameters
    sync_redis::instance().hdel(KEY_PULL_STATE, id.c_str());
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_pull(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    bool ok = sync_redis::instance().hdel(KEY_PULL_RECORD, id.c_str());
    if (!ok) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    // Also clean up corresponding state entry
    sync_redis::instance().hdel(KEY_PULL_STATE, id.c_str());
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

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
    if (!body.contains("enabled")) body["enabled"] = true;
    bool ok = sync_redis::instance().hsetnx(KEY_PUSH_RECORD, uid.c_str(), body.dump());
    if (!ok) { resp.status_code = 409; resp.body = R"({"error":"Push record already exists"})"; return; }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

void http_handler::handle_update_push(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    bool ok = sync_redis::instance().hset(KEY_PUSH_RECORD, id.c_str(), body.dump());
    if (!ok) { resp.status_code = 500; resp.body = R"({"error":"Redis error"})"; return; }
    // Delete status to force task restart with new parameters
    sync_redis::instance().hdel(KEY_PUSH_STATE, id.c_str());
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_push(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    bool ok = sync_redis::instance().hdel(KEY_PUSH_RECORD, id.c_str());
    if (!ok) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    // Also clean up corresponding state entry
    sync_redis::instance().hdel(KEY_PUSH_STATE, id.c_str());
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

IMPL_GET_ALL(handle_get_push_states, KEY_PUSH_STATE)

// ==================== Relay Start/Stop ====================

void http_handler::handle_relay_start(const HttpRequest &req, HttpResponse &resp)
{
    // URL: /api/relays/{pull|push}/start/{uid}
    std::string path = req.path;
    std::string uid = get_resource_id(req);
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }

    bool is_pull = path.find("/pull/") != std::string::npos;
    const char *key = is_pull ? KEY_PULL_RECORD : KEY_PUSH_RECORD;

    auto val = sync_redis::instance().hget(key, uid.c_str());
    if (val.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Record not found"})"; return; }

    try {
        json record = val.is_string() ? json::parse(val.get<std::string>()) : val;
        record["enabled"] = true;
        sync_redis::instance().hset(key, uid.c_str(), record.dump());
        resp.status_code = 200;
        resp.body = json{{"ok", true}, {"uid", uid}}.dump();
    } catch (...) {
        resp.status_code = 500; resp.body = R"({"error":"Failed to update record"})";
    }
}

void http_handler::handle_relay_stop(const HttpRequest &req, HttpResponse &resp)
{
    // URL: /api/relays/{pull|push}/stop/{uid}
    std::string path = req.path;
    std::string uid = get_resource_id(req);
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }

    bool is_pull = path.find("/pull/") != std::string::npos;
    const char *key = is_pull ? KEY_PULL_RECORD : KEY_PUSH_RECORD;

    auto val = sync_redis::instance().hget(key, uid.c_str());
    if (val.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Record not found"})"; return; }

    try {
        json record = val.is_string() ? json::parse(val.get<std::string>()) : val;
        record["enabled"] = false;
        sync_redis::instance().hset(key, uid.c_str(), record.dump());
        // Do NOT delete the status entry here — task distribution needs it to
        // detect the enabled=false mismatch and send an INACTIVE broadcast to
        // the node running the relay. Premature HDEL causes _pull/_push_status_map
        // to lose the entry on next sync, preventing the INACTIVE from ever firing,
        // which leaves stale MPT:REC records that keep refreshing.
        resp.status_code = 200;
        resp.body = json{{"ok", true}, {"uid", uid}}.dump();
    } catch (...) {
        resp.status_code = 500; resp.body = R"({"error":"Failed to update record"})";
    }
}

// ==================== Nodes (CASTER:NODE) read-only ====================

IMPL_GET_ALL(handle_get_nodes, KEY_CASTER_NODE)
IMPL_GET_ONE(handle_get_node, KEY_CASTER_NODE)

// ==================== Mountpoint Subscribers ====================

void http_handler::handle_get_mountpoint_subscribers(const HttpRequest &req, HttpResponse &resp)
{
    // Get all online mountpoints from MPT:LIST
    json mpts = sync_redis::instance().hgetall(KEY_MPT_ONLINE);
    json result = json::object();
    for (auto &[mpt_name, _] : mpts.items())
    {
        std::string sub_key = std::string(KEY_MPT_SUB) + ":" + mpt_name;
        long long count = sync_redis::instance().hlen(sub_key.c_str());
        result[mpt_name] = count;
    }
    resp.status_code = 200;
    resp.body = result.dump();
}

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
    status["ntrip_port"] = _config.ntrip_port;

    resp.status_code = 200;
    resp.body = status.dump();
}

void http_handler::handle_get_health(const HttpRequest &req, HttpResponse &resp)
{
    resp.status_code = 200;
    resp.body = R"({"status":"ok"})";
}

// ==================== Utility: Fetch Remote Sourcetable ====================

void http_handler::handle_fetch_sourcetable(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }

    std::string host = body.value("host", "");
    int port = body.value("port", 2101);
    std::string user = body.value("username", "");
    std::string pass = body.value("password", "");
    std::string ntrip_ver = body.value("ntrip_version", "2.0"); // "1.0" or "2.0"

    if (host.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing host"})"; return; }

    // Synchronous TCP connect + NTRIP sourcetable request
    int sock = -1;
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int gai = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (gai != 0 || !res)
    {
        resp.status_code = 502;
        resp.body = json{{"error", "DNS resolve failed"}, {"detail", gai_strerror(gai)}}.dump();
        if (res) freeaddrinfo(res);
        return;
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0)
    {
        freeaddrinfo(res);
        resp.status_code = 502;
        resp.body = R"({"error":"Socket creation failed"})";
        return;
    }

    // Set connect timeout (5 seconds)
    struct timeval tv{5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0)
    {
        freeaddrinfo(res);
        close(sock);
        resp.status_code = 502;
        resp.body = json{{"error", "Connection failed"}, {"detail", std::string(strerror(errno))}}.dump();
        return;
    }
    freeaddrinfo(res);

    // Build NTRIP sourcetable request based on version
    std::string request_str;
    if (ntrip_ver == "1.0")
    {
        // NTRIP 1.0: simple HTTP/1.0 request
        request_str = "GET / HTTP/1.0\r\nHost: " + host + ":" + std::to_string(port) + "\r\n"
                      "User-Agent: NTRIP NavCaster/1.0\r\n";
    }
    else
    {
        // NTRIP 2.0: HTTP/1.1 with Ntrip-Version header
        request_str = "GET / HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) + "\r\n"
                      "Ntrip-Version: Ntrip/2.0\r\n"
                      "User-Agent: NTRIP NavCaster/2.0\r\n"
                      "Connection: close\r\n";
    }
    if (!user.empty())
    {
        std::string credentials = user + ":" + pass;
        std::string encoded = util_base64_encode(credentials.c_str());
        request_str += "Authorization: Basic " + encoded + "\r\n";
    }
    request_str += "\r\n";

    ssize_t sent = send(sock, request_str.c_str(), request_str.size(), 0);
    if (sent <= 0)
    {
        close(sock);
        resp.status_code = 502;
        resp.body = R"({"error":"Send failed"})";
        return;
    }

    // Read response (sourcetable is typically small, 64KB buffer is plenty)
    std::string response;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0)
    {
        response.append(buf, n);
        if (response.size() > 65536) break; // Safety limit
    }
    close(sock);

    if (response.empty())
    {
        resp.status_code = 502;
        resp.body = R"({"error":"No response from server"})";
        return;
    }

    // Parse STR lines from NTRIP sourcetable
    json mountpoints = json::array();
    std::istringstream stream(response);
    std::string line;
    while (std::getline(stream, line))
    {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.substr(0, 4) == "STR;")
        {
            // STR;mountpoint;identifier;format;...
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lss(line);
            while (std::getline(lss, field, ';'))
                fields.push_back(field);

            if (fields.size() >= 2)
            {
                json entry;
                entry["mountpoint"] = fields[1];
                if (fields.size() > 2) entry["identifier"] = fields[2];
                if (fields.size() > 3) entry["format"] = fields[3];
                if (fields.size() > 4) entry["format_details"] = fields[4];
                if (fields.size() > 8) entry["country"] = fields[8];
                if (fields.size() > 9) entry["latitude"] = fields[9];
                if (fields.size() > 10) entry["longitude"] = fields[10];
                mountpoints.push_back(entry);
            }
        }
        if (line.find("ENDSOURCETABLE") != std::string::npos)
            break;
    }

    resp.status_code = 200;
    resp.body = json{{"ok", true}, {"mountpoints", mountpoints}}.dump();
}

void http_handler::handle_local_sourcetable(const HttpRequest &req, HttpResponse &resp)
{
    // 直接从 CasterCore 获取本地源表，不通过 NTRIP 协议（避免同线程阻塞死锁）
    std::string source_table_text = CASTER::Get_Source_Table_Text();

    // 解析 STR 行
    json mountpoints = json::array();
    std::istringstream stream(source_table_text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.substr(0, 4) == "STR;")
        {
            std::vector<std::string> fields;
            std::string field;
            std::istringstream lss(line);
            while (std::getline(lss, field, ';'))
                fields.push_back(field);

            if (fields.size() >= 2)
            {
                json entry;
                entry["mountpoint"] = fields[1];
                if (fields.size() > 2) entry["identifier"] = fields[2];
                if (fields.size() > 3) entry["format"] = fields[3];
                if (fields.size() > 4) entry["format_details"] = fields[4];
                if (fields.size() > 8) entry["country"] = fields[8];
                if (fields.size() > 9) entry["latitude"] = fields[9];
                if (fields.size() > 10) entry["longitude"] = fields[10];
                mountpoints.push_back(entry);
            }
        }
    }

    resp.status_code = 200;
    resp.body = json{{"ok", true}, {"mountpoints", mountpoints}}.dump();
}

// ==================== Configuration ====================

void http_handler::save_config(const std::string &section, const std::string &json_str)
{
    const char *key = nullptr;
    if (section == "service") key = KEY_CONF_SERVICE;
    else if (section == "core") key = KEY_CONF_CORE;
    else if (section == "auth") key = KEY_CONF_AUTH;
    else return;
    sync_redis::instance().set(key, json_str);
}

void http_handler::handle_get_configs(const HttpRequest &req, HttpResponse &resp)
{
    json result = json::object();
    auto service = sync_redis::instance().get(KEY_CONF_SERVICE);
    auto core = sync_redis::instance().get(KEY_CONF_CORE);
    auto auth = sync_redis::instance().get(KEY_CONF_AUTH);
    if (!service.is_null()) result["service"] = service;
    if (!core.is_null()) result["core"] = core;
    if (!auth.is_null()) result["auth"] = auth;
    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_get_config(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    const char *key = nullptr;
    if (id == "service") key = KEY_CONF_SERVICE;
    else if (id == "core") key = KEY_CONF_CORE;
    else if (id == "auth") key = KEY_CONF_AUTH;
    else { resp.status_code = 404; resp.body = R"({"error":"Unknown config section"})"; return; }

    json data = sync_redis::instance().get(key);

    // For auth config, return default username if Redis has no entry, and never expose password
    if (id == "auth")
    {
        json result;
        if (data.is_object() && data.contains("admin_user"))
            result["admin_user"] = data["admin_user"];
        else
            result["admin_user"] = _config.admin_user;
        resp.status_code = 200;
        resp.body = result.dump();
        return;
    }

    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Config not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_update_config(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    const char *key = nullptr;
    if (id == "service") key = KEY_CONF_SERVICE;
    else if (id == "core") key = KEY_CONF_CORE;
    else if (id == "auth") key = KEY_CONF_AUTH;
    else { resp.status_code = 404; resp.body = R"({"error":"Unknown config section"})"; return; }

    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }

    // For auth config, require old password verification
    if (id == "auth")
    {
        std::string old_password = body.value("old_password", "");
        if (old_password.empty())
        {
            resp.status_code = 400;
            resp.body = R"({"error":"需要输入当前密码"})";
            return;
        }

        // Get current password: check Redis first, then fall back to config defaults
        std::string current_password = _config.admin_password;
        json auth_conf = sync_redis::instance().get(KEY_CONF_AUTH);
        if (auth_conf.is_object() && auth_conf.contains("admin_password"))
        {
            current_password = auth_conf.value("admin_password", _config.admin_password);
        }

        if (old_password != current_password)
        {
            resp.status_code = 403;
            resp.body = R"({"error":"当前密码错误"})";
            return;
        }

        // Remove old_password from the body before saving
        body.erase("old_password");
    }

    bool ok = sync_redis::instance().set(key, body.dump());
    if (!ok) { resp.status_code = 500; resp.body = R"({"error":"Redis error"})"; return; }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
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
