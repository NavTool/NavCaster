#include "http_handler.h"
#include "SysUsage.h"
#include "Caster_Core.h"
#include "base64.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <set>
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
static const char *KEY_LOG_MPT = "LOG:MPT";
static const char *KEY_LOG_USR = "LOG:USR";
static const char *KEY_LOG_AUDIT = "LOG:AUDIT";
static const int AUDIT_LOG_MAX = 5000;

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
                spdlog::warn("[sync_redis]: HGETALL {} failed, reply is null, reconnecting", key);
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
                spdlog::warn("[sync_redis]: HGET {} {} failed, reply is null, reconnecting", key, field);
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

        bool setex(const char *key, int seconds, const std::string &value)
        {
            if (!ensure_connected())
                return false;

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "SETEX %s %d %s", key, seconds, value.c_str()));
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

        // LPUSH key value → number of elements after push
        long long lpush(const char *key, const std::string &value)
        {
            if (!ensure_connected())
                return -1;
            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "LPUSH %s %s", key, value.c_str()));
            long long count = -1;
            if (reply && reply->type == REDIS_REPLY_INTEGER)
                count = reply->integer;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return count;
        }

        // LTRIM key start stop
        bool ltrim(const char *key, long long start, long long stop)
        {
            if (!ensure_connected())
                return false;
            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "LTRIM %s %lld %lld", key, start, stop));
            bool ok = reply && reply->type != REDIS_REPLY_ERROR;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return ok;
        }

        // LRANGE key start stop → json array of parsed values
        json lrange(const char *key, long long start, long long stop)
        {
            if (!ensure_connected())
                return json::array();

            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "LRANGE %s %lld %lld", key, start, stop));
            if (!reply)
            {
                reconnect();
                return json::array();
            }

            json result = json::array();
            if (reply->type == REDIS_REPLY_ARRAY)
            {
                for (size_t i = 0; i < reply->elements; i++)
                {
                    std::string value = reply->element[i]->str ? reply->element[i]->str : "";
                    try
                    {
                        result.push_back(json::parse(value));
                    }
                    catch (...)
                    {
                        result.push_back(value);
                    }
                }
            }
            freeReplyObject(reply);
            return result;
        }

        // INFO [section] → raw info string
        std::string info(const char *section = nullptr)
        {
            if (!ensure_connected())
                return "";

            redisReply *reply;
            if (section)
                reply = static_cast<redisReply *>(redisCommand(_ctx, "INFO %s", section));
            else
                reply = static_cast<redisReply *>(redisCommand(_ctx, "INFO"));
            if (!reply)
            {
                reconnect();
                return "";
            }
            std::string result;
            if (reply->type == REDIS_REPLY_STRING && reply->str)
                result = reply->str;
            freeReplyObject(reply);
            return result;
        }

        // DBSIZE → number of keys
        long long dbsize()
        {
            if (!ensure_connected())
                return 0;

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "DBSIZE"));
            long long count = 0;
            if (reply && reply->type == REDIS_REPLY_INTEGER)
                count = reply->integer;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return count;
        }

        // SCAN all keys → vector of key names
        std::vector<std::string> scan_all_keys(int batch = 1000)
        {
            if (!ensure_connected())
                return {};

            std::vector<std::string> keys;
            unsigned long long cursor = 0;
            do
            {
                auto *reply = static_cast<redisReply *>(
                    redisCommand(_ctx, "SCAN %llu COUNT %d", cursor, batch));
                if (!reply)
                {
                    reconnect();
                    break;
                }
                if (reply->type == REDIS_REPLY_ARRAY && reply->elements == 2)
                {
                    cursor = std::strtoull(reply->element[0]->str, nullptr, 10);
                    auto *arr = reply->element[1];
                    for (size_t i = 0; i < arr->elements; i++)
                    {
                        if (arr->element[i]->str)
                            keys.emplace_back(arr->element[i]->str);
                    }
                }
                else
                {
                    freeReplyObject(reply);
                    break;
                }
                freeReplyObject(reply);
            } while (cursor != 0);
            return keys;
        }

        // TYPE key → string
        std::string type(const char *key)
        {
            if (!ensure_connected())
                return "none";

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "TYPE %s", key));
            std::string result = "none";
            if (reply && reply->type == REDIS_REPLY_STATUS && reply->str)
                result = reply->str;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return result;
        }

        // LLEN key → list length
        long long llen(const char *key)
        {
            if (!ensure_connected())
                return 0;

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "LLEN %s", key));
            long long count = 0;
            if (reply && reply->type == REDIS_REPLY_INTEGER)
                count = reply->integer;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return count;
        }

        // MEMORY USAGE key → bytes (Redis 4.0+)
        long long memory_usage(const char *key)
        {
            if (!ensure_connected())
                return 0;

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "MEMORY USAGE %s", key));
            long long bytes = 0;
            if (reply && reply->type == REDIS_REPLY_INTEGER)
                bytes = reply->integer;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return bytes;
        }

        // TTL key → seconds (-1 no expire, -2 key missing)
        long long ttl(const char *key)
        {
            if (!ensure_connected())
                return -2;

            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "TTL %s", key));
            long long t = -2;
            if (reply && reply->type == REDIS_REPLY_INTEGER)
                t = reply->integer;
            if (reply)
                freeReplyObject(reply);
            else
                reconnect();
            return t;
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
    _server.route(EVHTTP_REQ_GET, "/api/nodes/config/*", [this](auto &req, auto &resp)
                  { handle_get_node_config(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/nodes/history/*", [this](auto &req, auto &resp)
                  { handle_get_node_history(req, resp); });
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

    // ==================== Connection History ====================
    _server.route(EVHTTP_REQ_GET, "/api/logs/servers", [this](auto &req, auto &resp)
                  { handle_get_server_logs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/logs/clients", [this](auto &req, auto &resp)
                  { handle_get_client_logs(req, resp); });

    // ==================== Statistics ====================
    _server.route(EVHTTP_REQ_GET, "/api/stats/overview", [this](auto &req, auto &resp)
                  { handle_get_stats_overview(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/daily/*", [this](auto &req, auto &resp)
                  { handle_get_stats_daily(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/mountpoints/ranking", [this](auto &req, auto &resp)
                  { handle_get_stats_mpt_ranking(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/mountpoints/history/*", [this](auto &req, auto &resp)
                  { handle_get_stats_mpt_history(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/users/ranking", [this](auto &req, auto &resp)
                  { handle_get_stats_usr_ranking(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/users/history/*", [this](auto &req, auto &resp)
                  { handle_get_stats_usr_history(req, resp); });

    // ==================== Configuration ====================
    _server.route(EVHTTP_REQ_GET, "/api/config", [this](auto &req, auto &resp)
                  { handle_get_configs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/config/*", [this](auto &req, auto &resp)
                  { handle_get_config(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/config/*", [this](auto &req, auto &resp)
                  { handle_update_config(req, resp); });

    // ==================== Monitoring ====================
    _server.route(EVHTTP_REQ_GET, "/api/monitor/redis", [this](auto &req, auto &resp)
                  { handle_get_monitor_redis(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/monitor/redis/keys", [this](auto &req, auto &resp)
                  { handle_get_monitor_redis_keys(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/monitor/cluster", [this](auto &req, auto &resp)
                  { handle_get_monitor_cluster(req, resp); });

    // ==================== Node Control ====================
    _server.route(EVHTTP_REQ_POST, "/api/nodes/action/*", [this](auto &req, auto &resp)
                  { handle_post_node_action(req, resp); });

    // ==================== Audit Log ====================
    _server.route(EVHTTP_REQ_GET, "/api/logs/audit", [this](auto &req, auto &resp)
                  { handle_get_audit_logs(req, resp); });

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
        spdlog::info("[{}:{}]: Login success, user: {}", __class__, __func__, user);
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
            spdlog::warn("[{}:{}]: Login failed, user: {}", __class__, __func__, user);
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
    sync_redis::instance().publish("CASTER:CONF", "ALIAS");
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"alias", uid}}.dump();
}

void http_handler::handle_update_alias(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    bool ok = sync_redis::instance().hset(KEY_ALIAS_RULE, id.c_str(), body.dump());
    if (!ok) { resp.status_code = 500; resp.body = R"({"error":"Redis error"})"; return; }
    sync_redis::instance().publish("CASTER:CONF", "ALIAS");
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_alias(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    bool ok = sync_redis::instance().hdel(KEY_ALIAS_RULE, id.c_str());
    if (!ok) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    sync_redis::instance().publish("CASTER:CONF", "ALIAS");
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

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

// ==================== Node History (NODE:HISTORY:*) read-only ====================

void http_handler::handle_get_node_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string node_id = get_resource_id(req);
    if (node_id.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing node ID"})";
        return;
    }

    // range 参数决定读取哪个层级: raw(默认) / 1m / 5m
    std::string range = "raw";
    auto range_it = req.query_params.find("range");
    if (range_it != req.query_params.end())
    {
        range = range_it->second;
    }

    // limit 参数
    long long limit = 17280;
    auto it = req.query_params.find("limit");
    if (it != req.query_params.end())
    {
        try { limit = std::stoll(it->second); } catch (...) {}
        if (limit <= 0) limit = 17280;
    }

    // 根据 range 选择 Redis key 和最大限制
    std::string key;
    long long max_limit;
    if (range == "5m")
    {
        key = "NODE:HISTORY:" + node_id + ":5M";
        max_limit = 96480;
    }
    else if (range == "1m")
    {
        key = "NODE:HISTORY:" + node_id + ":1M";
        max_limit = 33120;
    }
    else
    {
        key = "NODE:HISTORY:" + node_id;
        max_limit = 120960;
    }

    if (limit > max_limit) limit = max_limit;

    json data = sync_redis::instance().lrange(key.c_str(), 0, limit - 1);
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Connection History (LOG:MPT / LOG:USR) read-only ====================

static bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

static void merge_log_hash(json &result, const std::string &source_key, const json &entries)
{
    if (!entries.is_object())
        return;

    for (auto &[field, entry] : entries.items())
    {
        result[source_key + "|" + field] = entry;
    }
}

static json collect_connection_logs(sync_redis &redis, const std::string &hierarchical_prefix, const char *legacy_key)
{
    json result = json::object();

    merge_log_hash(result, legacy_key, redis.hgetall(legacy_key));

    auto keys = redis.scan_all_keys();
    for (const auto &key : keys)
    {
        if (starts_with(key, hierarchical_prefix))
            merge_log_hash(result, key, redis.hgetall(key.c_str()));
    }

    return result;
}

static json collect_named_connection_logs(sync_redis &redis, const std::string &hierarchical_key,
                                          const char *legacy_key, const std::string &name)
{
    json result = json::object();
    merge_log_hash(result, hierarchical_key, redis.hgetall(hierarchical_key.c_str()));

    json legacy = redis.hgetall(legacy_key);
    if (legacy.is_object())
    {
        for (auto &[field, entry] : legacy.items())
        {
            if (!entry.is_object())
                continue;
            if (entry.value("name", "") != name)
                continue;
            result[std::string(legacy_key) + "|" + field] = entry;
        }
    }

    return result;
}

void http_handler::handle_get_server_logs(const HttpRequest &req, HttpResponse &resp)
{
    auto &redis = sync_redis::instance();
    json result = collect_connection_logs(redis, "LOG:MPT:", KEY_LOG_MPT);
    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_get_client_logs(const HttpRequest &req, HttpResponse &resp)
{
    auto &redis = sync_redis::instance();
    json result = collect_connection_logs(redis, "LOG:USR:", KEY_LOG_USR);
    resp.status_code = 200;
    resp.body = result.dump();
}

// ==================== Statistics ====================

// Helper: parse "YYYY-MM-DD" or unix timestamp from query param, return 0 if absent
static long long parse_time_param(const std::unordered_map<std::string, std::string> &params, const char *name)
{
    auto it = params.find(name);
    if (it == params.end() || it->second.empty()) return 0;
    // Try unix timestamp first
    try { return std::stoll(it->second); } catch (...) {}
    // Try date string "YYYY-MM-DD"
    struct tm tm_val{};
    if (strptime(it->second.c_str(), "%Y-%m-%d", &tm_val))
        return mktime(&tm_val);
    return 0;
}

// GET /api/stats/overview?start=&end=&date=
// Returns aggregated stats from LOG:MPT + LOG:USR within time range
void http_handler::handle_get_stats_overview(const HttpRequest &req, HttpResponse &resp)
{
    // Determine time range
    long long now_ts = static_cast<long long>(time(nullptr));
    long long start_ts = parse_time_param(req.query_params, "start");
    long long end_ts = parse_time_param(req.query_params, "end");

    // If "date" param given (YYYY-MM-DD), use that day
    auto date_it = req.query_params.find("date");
    if (date_it != req.query_params.end() && !date_it->second.empty())
    {
        struct tm tm_val{};
        if (strptime(date_it->second.c_str(), "%Y-%m-%d", &tm_val))
        {
            start_ts = mktime(&tm_val);
            tm_val.tm_mday += 1;
            end_ts = mktime(&tm_val);
        }
    }

    // Default: today
    if (start_ts == 0)
    {
        struct tm tm_today{};
        time_t t = time(nullptr);
        localtime_r(&t, &tm_today);
        tm_today.tm_hour = 0; tm_today.tm_min = 0; tm_today.tm_sec = 0;
        start_ts = mktime(&tm_today);
    }
    if (end_ts == 0) end_ts = now_ts + 1;

    auto &redis = sync_redis::instance();
    json mpt_logs = collect_connection_logs(redis, "LOG:MPT:", KEY_LOG_MPT);
    json usr_logs = collect_connection_logs(redis, "LOG:USR:", KEY_LOG_USR);

    // Aggregate
    long long mpt_connections = 0, usr_connections = 0;
    long long total_duration_mpt = 0, total_duration_usr = 0;
    int peak_concurrent_mpt = 0, peak_concurrent_usr = 0;
    std::set<std::string> unique_mounts, unique_users;

    // Time-slot concurrency (hourly buckets for the queried range, max 168 buckets = 7 days)
    int num_hours = std::min(168LL, (end_ts - start_ts + 3599) / 3600);
    if (num_hours <= 0) num_hours = 24;
    std::vector<int> mpt_hourly(num_hours, 0);
    std::vector<int> usr_hourly(num_hours, 0);

    auto process_logs = [&](const json &logs, bool is_mpt)
    {
        for (auto &[field, entry] : logs.items())
        {
            if (!entry.is_object()) continue;
            long long ct = entry.value("connect_time", 0LL);
            long long dt = entry.value("disconnect_time", 0LL);
            if (dt == 0) dt = now_ts; // still online

            // Skip if completely outside range
            if (dt < start_ts || ct >= end_ts) continue;

            if (is_mpt)
            {
                mpt_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_mounts.insert(name);
                long long overlap_start = std::max(ct, start_ts);
                long long overlap_end = std::min(dt, end_ts);
                total_duration_mpt += (overlap_end - overlap_start);
            }
            else
            {
                usr_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_users.insert(name);
                long long overlap_start = std::max(ct, start_ts);
                long long overlap_end = std::min(dt, end_ts);
                total_duration_usr += (overlap_end - overlap_start);
            }

            // Hourly concurrency: mark each hour this session overlaps
            long long h_start = std::max(ct, start_ts);
            long long h_end = std::min(dt, end_ts);
            int bucket_begin = static_cast<int>((h_start - start_ts) / 3600);
            int bucket_end = static_cast<int>((h_end - start_ts) / 3600);
            if (bucket_begin < 0) bucket_begin = 0;
            if (bucket_end >= num_hours) bucket_end = num_hours - 1;
            auto &hourly = is_mpt ? mpt_hourly : usr_hourly;
            for (int b = bucket_begin; b <= bucket_end; b++)
                hourly[b]++;
        }
    };

    process_logs(mpt_logs, true);
    process_logs(usr_logs, false);

    for (int i = 0; i < num_hours; i++)
    {
        if (mpt_hourly[i] > peak_concurrent_mpt) peak_concurrent_mpt = mpt_hourly[i];
        if (usr_hourly[i] > peak_concurrent_usr) peak_concurrent_usr = usr_hourly[i];
    }

    // Build hourly trend
    json hourly_trend = json::array();
    for (int i = 0; i < num_hours; i++)
    {
        json h;
        h["ts"] = start_ts + i * 3600;
        h["mpt"] = mpt_hourly[i];
        h["usr"] = usr_hourly[i];
        hourly_trend.push_back(h);
    }

    json result;
    result["start"] = start_ts;
    result["end"] = end_ts;
    result["mpt_connections"] = mpt_connections;
    result["usr_connections"] = usr_connections;
    result["peak_concurrent_mpt"] = peak_concurrent_mpt;
    result["peak_concurrent_usr"] = peak_concurrent_usr;
    result["avg_duration_mpt"] = mpt_connections > 0 ? total_duration_mpt / mpt_connections : 0;
    result["avg_duration_usr"] = usr_connections > 0 ? total_duration_usr / usr_connections : 0;
    result["unique_mountpoints"] = static_cast<int>(unique_mounts.size());
    result["unique_users"] = static_cast<int>(unique_users.size());
    result["hourly_trend"] = hourly_trend;

    resp.status_code = 200;
    resp.body = result.dump();
}

// GET /api/stats/daily/{YYYY-MM-DD}
// Returns daily stats, cached in Redis STAT:DAILY:{date} for past dates
void http_handler::handle_get_stats_daily(const HttpRequest &req, HttpResponse &resp)
{
    std::string date_str = get_resource_id(req); // YYYY-MM-DD
    if (date_str.size() != 10 || date_str[4] != '-' || date_str[7] != '-')
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Invalid date format, use YYYY-MM-DD"})";
        return;
    }

    // Parse date to start_ts / end_ts
    struct tm tm_val{};
    if (!strptime(date_str.c_str(), "%Y-%m-%d", &tm_val))
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Invalid date"})";
        return;
    }
    long long start_ts = mktime(&tm_val);
    tm_val.tm_mday += 1;
    long long end_ts = mktime(&tm_val);
    long long now_ts = static_cast<long long>(time(nullptr));

    // Determine if this is a past day (can cache)
    bool is_past = end_ts <= now_ts;

    // Check cache for past dates
    auto &redis = sync_redis::instance();
    std::string cache_key = "STAT:DAILY:" + date_str;
    if (is_past)
    {
        auto cached = redis.get(cache_key.c_str());
        if (!cached.is_null())
        {
            resp.status_code = 200;
            resp.body = cached.is_string() ? cached.get<std::string>() : cached.dump();
            return;
        }
    }

    // Compute from logs
    json mpt_logs = collect_connection_logs(redis, "LOG:MPT:", KEY_LOG_MPT);
    json usr_logs = collect_connection_logs(redis, "LOG:USR:", KEY_LOG_USR);

    long long mpt_connections = 0, usr_connections = 0;
    long long total_duration_mpt = 0, total_duration_usr = 0;
    int peak_concurrent_mpt = 0, peak_concurrent_usr = 0;
    std::set<std::string> unique_mounts, unique_users;

    int num_hours = 24;
    std::vector<int> mpt_hourly(num_hours, 0);
    std::vector<int> usr_hourly(num_hours, 0);

    auto process_logs = [&](const json &logs, bool is_mpt)
    {
        for (auto &[field, entry] : logs.items())
        {
            if (!entry.is_object()) continue;
            long long ct = entry.value("connect_time", 0LL);
            long long dt = entry.value("disconnect_time", 0LL);
            if (dt == 0) dt = now_ts;
            if (dt < start_ts || ct >= end_ts) continue;

            if (is_mpt)
            {
                mpt_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_mounts.insert(name);
                total_duration_mpt += (std::min(dt, end_ts) - std::max(ct, start_ts));
            }
            else
            {
                usr_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_users.insert(name);
                total_duration_usr += (std::min(dt, end_ts) - std::max(ct, start_ts));
            }

            long long h_start = std::max(ct, start_ts);
            long long h_end = std::min(dt, end_ts);
            int bucket_begin = std::max(0, static_cast<int>((h_start - start_ts) / 3600));
            int bucket_end = std::min(num_hours - 1, static_cast<int>((h_end - start_ts) / 3600));
            auto &hourly = is_mpt ? mpt_hourly : usr_hourly;
            for (int b = bucket_begin; b <= bucket_end; b++)
                hourly[b]++;
        }
    };

    process_logs(mpt_logs, true);
    process_logs(usr_logs, false);

    for (int i = 0; i < num_hours; i++)
    {
        if (mpt_hourly[i] > peak_concurrent_mpt) peak_concurrent_mpt = mpt_hourly[i];
        if (usr_hourly[i] > peak_concurrent_usr) peak_concurrent_usr = usr_hourly[i];
    }

    json hourly_trend = json::array();
    for (int i = 0; i < num_hours; i++)
    {
        json h;
        h["ts"] = start_ts + i * 3600;
        h["mpt"] = mpt_hourly[i];
        h["usr"] = usr_hourly[i];
        hourly_trend.push_back(h);
    }

    json result;
    result["date"] = date_str;
    result["start"] = start_ts;
    result["end"] = end_ts;
    result["mpt_connections"] = mpt_connections;
    result["usr_connections"] = usr_connections;
    result["peak_concurrent_mpt"] = peak_concurrent_mpt;
    result["peak_concurrent_usr"] = peak_concurrent_usr;
    result["avg_duration_mpt"] = mpt_connections > 0 ? total_duration_mpt / mpt_connections : 0;
    result["avg_duration_usr"] = usr_connections > 0 ? total_duration_usr / usr_connections : 0;
    result["unique_mountpoints"] = static_cast<int>(unique_mounts.size());
    result["unique_users"] = static_cast<int>(unique_users.size());
    result["hourly_trend"] = hourly_trend;

    std::string body = result.dump();

    // Cache past dates (TTL 7 days = 604800s)
    if (is_past)
        redis.setex(cache_key.c_str(), 604800, body);

    resp.status_code = 200;
    resp.body = std::move(body);
}

// GET /api/stats/mountpoints/ranking?start=&end=&limit=20
void http_handler::handle_get_stats_mpt_ranking(const HttpRequest &req, HttpResponse &resp)
{
    long long now_ts = static_cast<long long>(time(nullptr));
    long long start_ts = parse_time_param(req.query_params, "start");
    long long end_ts = parse_time_param(req.query_params, "end");
    if (start_ts == 0) { struct tm t{}; time_t tt = time(nullptr); localtime_r(&tt, &t); t.tm_hour=0;t.tm_min=0;t.tm_sec=0; start_ts = mktime(&t); }
    if (end_ts == 0) end_ts = now_ts + 1;

    int limit = 20;
    auto lit = req.query_params.find("limit");
    if (lit != req.query_params.end()) { try { limit = std::stoi(lit->second); } catch (...) {} }
    if (limit <= 0) limit = 20;
    if (limit > 100) limit = 100;

    auto &redis = sync_redis::instance();
    json mpt_logs = collect_connection_logs(redis, "LOG:MPT:", KEY_LOG_MPT);

    // Aggregate per mountpoint
    struct MptStat { long long total_duration = 0; int connections = 0; long long last_seen = 0; };
    std::map<std::string, MptStat> stats;

    for (auto &[field, entry] : mpt_logs.items())
    {
        if (!entry.is_object()) continue;
        long long ct = entry.value("connect_time", 0LL);
        long long dt = entry.value("disconnect_time", 0LL);
        if (dt == 0) dt = now_ts;
        if (dt < start_ts || ct >= end_ts) continue;

        std::string name = entry.value("name", "");
        if (name.empty()) continue;

        long long overlap = std::min(dt, end_ts) - std::max(ct, start_ts);
        auto &s = stats[name];
        s.total_duration += overlap;
        s.connections++;
        if (dt > s.last_seen) s.last_seen = dt;
    }

    // Sort by total_duration desc
    std::vector<std::pair<std::string, MptStat>> sorted(stats.begin(), stats.end());
    std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) { return a.second.total_duration > b.second.total_duration; });

    json result = json::array();
    int count = 0;
    for (auto &[name, s] : sorted)
    {
        if (count >= limit) break;
        json item;
        item["name"] = name;
        item["total_duration"] = s.total_duration;
        item["connections"] = s.connections;
        item["last_seen"] = s.last_seen;
        result.push_back(item);
        count++;
    }

    resp.status_code = 200;
    resp.body = result.dump();
}

// GET /api/stats/users/ranking?start=&end=&limit=20
void http_handler::handle_get_stats_usr_ranking(const HttpRequest &req, HttpResponse &resp)
{
    long long now_ts = static_cast<long long>(time(nullptr));
    long long start_ts = parse_time_param(req.query_params, "start");
    long long end_ts = parse_time_param(req.query_params, "end");
    if (start_ts == 0) { struct tm t{}; time_t tt = time(nullptr); localtime_r(&tt, &t); t.tm_hour=0;t.tm_min=0;t.tm_sec=0; start_ts = mktime(&t); }
    if (end_ts == 0) end_ts = now_ts + 1;

    int limit = 20;
    auto lit = req.query_params.find("limit");
    if (lit != req.query_params.end()) { try { limit = std::stoi(lit->second); } catch (...) {} }
    if (limit <= 0) limit = 20;
    if (limit > 100) limit = 100;

    auto &redis = sync_redis::instance();
    json usr_logs = collect_connection_logs(redis, "LOG:USR:", KEY_LOG_USR);

    struct UsrStat { long long total_duration = 0; int connections = 0; long long last_seen = 0; std::set<std::string> mounts; };
    std::map<std::string, UsrStat> stats;

    for (auto &[field, entry] : usr_logs.items())
    {
        if (!entry.is_object()) continue;
        long long ct = entry.value("connect_time", 0LL);
        long long dt = entry.value("disconnect_time", 0LL);
        if (dt == 0) dt = now_ts;
        if (dt < start_ts || ct >= end_ts) continue;

        std::string name = entry.value("name", "");
        if (name.empty()) continue;
        std::string mount = entry.value("mount", "");

        long long overlap = std::min(dt, end_ts) - std::max(ct, start_ts);
        auto &s = stats[name];
        s.total_duration += overlap;
        s.connections++;
        if (dt > s.last_seen) s.last_seen = dt;
        if (!mount.empty()) s.mounts.insert(mount);
    }

    std::vector<std::pair<std::string, UsrStat>> sorted(stats.begin(), stats.end());
    std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) { return a.second.total_duration > b.second.total_duration; });

    json result = json::array();
    int count = 0;
    for (auto &[name, s] : sorted)
    {
        if (count >= limit) break;
        json item;
        item["name"] = name;
        item["total_duration"] = s.total_duration;
        item["connections"] = s.connections;
        item["last_seen"] = s.last_seen;
        item["mount_count"] = static_cast<int>(s.mounts.size());
        result.push_back(item);
        count++;
    }

    resp.status_code = 200;
    resp.body = result.dump();
}

// GET /api/stats/mountpoints/history/{mount} — connection history for a specific mountpoint
void http_handler::handle_get_stats_mpt_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string mount = get_resource_id(req);
    if (mount.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing mountpoint name"})"; return; }

    auto &redis = sync_redis::instance();
    json mpt_logs = collect_named_connection_logs(redis, "LOG:MPT:" + mount, KEY_LOG_MPT, mount);
    long long now_ts = static_cast<long long>(time(nullptr));

    json result = json::array();
    for (auto &[field, entry] : mpt_logs.items())
    {
        if (!entry.is_object()) continue;
        std::string name = entry.value("name", "");
        if (name != mount) continue;

        json item = entry;
        // Add computed duration
        long long ct = entry.value("connect_time", 0LL);
        long long dt = entry.value("disconnect_time", 0LL);
        item["duration"] = (dt > 0 ? dt : now_ts) - ct;
        item["online"] = (dt == 0);
        result.push_back(item);
    }

    // Sort by connect_time descending (newest first)
    std::sort(result.begin(), result.end(), [](const json &a, const json &b)
    { return a.value("connect_time", 0LL) > b.value("connect_time", 0LL); });

    resp.status_code = 200;
    resp.body = result.dump();
}

// GET /api/stats/users/history/{user} — connection history for a specific user
void http_handler::handle_get_stats_usr_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string user = get_resource_id(req);
    if (user.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing user name"})"; return; }

    auto &redis = sync_redis::instance();
    json usr_logs = collect_named_connection_logs(redis, "LOG:USR:" + user, KEY_LOG_USR, user);
    long long now_ts = static_cast<long long>(time(nullptr));

    json result = json::array();
    for (auto &[field, entry] : usr_logs.items())
    {
        if (!entry.is_object()) continue;
        std::string name = entry.value("name", "");
        if (name != user) continue;

        json item = entry;
        long long ct = entry.value("connect_time", 0LL);
        long long dt = entry.value("disconnect_time", 0LL);
        item["duration"] = (dt > 0 ? dt : now_ts) - ct;
        item["online"] = (dt == 0);
        result.push_back(item);
    }

    std::sort(result.begin(), result.end(), [](const json &a, const json &b)
    { return a.value("connect_time", 0LL) > b.value("connect_time", 0LL); });

    resp.status_code = 200;
    resp.body = result.dump();
}

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

    // Cluster master info
    auto &redis = sync_redis::instance();
    json master_val = redis.get("CASTER:MASTER");
    if (master_val.is_string())
        status["master_node"] = master_val.get<std::string>();
    else
        status["master_node"] = nullptr;

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

// ==================== Monitoring Handlers ====================

// Parse Redis INFO text into structured JSON
static json parse_redis_info(const std::string &info_text)
{
    json result = json::object();
    std::string current_section;
    std::istringstream stream(info_text);
    std::string line;

    while (std::getline(stream, line))
    {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;

        // Section header: # Server
        if (line.size() > 2 && line[0] == '#')
        {
            current_section = line.substr(2);
            // lowercase section name
            std::transform(current_section.begin(), current_section.end(), current_section.begin(), ::tolower);
            result[current_section] = json::object();
            continue;
        }

        // key:value pair
        auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Try to parse as number
        json jval;
        try
        {
            size_t pos = 0;
            if (value.find('.') != std::string::npos)
            {
                double d = std::stod(value, &pos);
                if (pos == value.size())
                    jval = d;
                else
                    jval = value;
            }
            else
            {
                long long ll = std::stoll(value, &pos);
                if (pos == value.size())
                    jval = ll;
                else
                    jval = value;
            }
        }
        catch (...)
        {
            jval = value;
        }

        if (!current_section.empty() && result.contains(current_section))
            result[current_section][key] = jval;
        else
            result[key] = jval;
    }
    return result;
}

void http_handler::handle_get_monitor_redis(const HttpRequest &req, HttpResponse &resp)
{
    auto &redis = sync_redis::instance();
    auto info_raw = redis.info();
    if (info_raw.empty())
    {
        resp.status_code = 503;
        resp.body = R"({"error":"Redis not available"})";
        return;
    }

    auto info = parse_redis_info(info_raw);

    // Build structured response with key metrics
    json result = json::object();

    // Server info
    if (info.contains("server"))
    {
        auto &s = info["server"];
        result["server"] = {
            {"redis_version", s.value("redis_version", "")},
            {"uptime_in_seconds", s.value("uptime_in_seconds", 0)},
            {"tcp_port", s.value("tcp_port", 0)},
            {"os", s.value("os", "")},
            {"process_id", s.value("process_id", 0)}};
    }

    // Client info
    if (info.contains("clients"))
    {
        auto &c = info["clients"];
        result["clients"] = {
            {"connected_clients", c.value("connected_clients", 0)},
            {"blocked_clients", c.value("blocked_clients", 0)},
            {"maxclients", c.value("maxclients", 0)}};
    }

    // Memory info
    if (info.contains("memory"))
    {
        auto &m = info["memory"];
        result["memory"] = {
            {"used_memory", m.value("used_memory", 0)},
            {"used_memory_human", m.value("used_memory_human", "")},
            {"used_memory_rss", m.value("used_memory_rss", 0)},
            {"used_memory_rss_human", m.value("used_memory_rss_human", "")},
            {"used_memory_peak", m.value("used_memory_peak", 0)},
            {"used_memory_peak_human", m.value("used_memory_peak_human", "")},
            {"mem_fragmentation_ratio", m.value("mem_fragmentation_ratio", 0.0)}};
    }

    // Stats
    if (info.contains("stats"))
    {
        auto &st = info["stats"];
        long long hits = st.value("keyspace_hits", 0LL);
        long long misses = st.value("keyspace_misses", 0LL);
        double hit_rate = (hits + misses > 0) ? static_cast<double>(hits) / (hits + misses) : 0.0;
        result["stats"] = {
            {"total_connections_received", st.value("total_connections_received", 0)},
            {"total_commands_processed", st.value("total_commands_processed", 0)},
            {"instantaneous_ops_per_sec", st.value("instantaneous_ops_per_sec", 0)},
            {"keyspace_hits", hits},
            {"keyspace_misses", misses},
            {"hit_rate", hit_rate},
            {"instantaneous_input_kbps", st.value("instantaneous_input_kbps", 0.0)},
            {"instantaneous_output_kbps", st.value("instantaneous_output_kbps", 0.0)}};
    }

    // Replication
    if (info.contains("replication"))
    {
        auto &r = info["replication"];
        result["replication"] = {
            {"role", r.value("role", "")},
            {"connected_slaves", r.value("connected_slaves", 0)}};
    }

    // Keyspace
    if (info.contains("keyspace"))
    {
        result["keyspace"] = info["keyspace"];
    }

    // Total keys
    result["total_keys"] = redis.dbsize();

    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_get_monitor_redis_keys(const HttpRequest &req, HttpResponse &resp)
{
    auto &redis = sync_redis::instance();

    // SCAN all keys and aggregate by prefix
    auto keys = redis.scan_all_keys();

    // Known business key descriptions
    static const std::unordered_map<std::string, std::string> key_descriptions = {
        {"MPT:STAT", "基站在线状态"},
        {"MPT:RECORD", "源列表记录"},
        {"MPT:LIST", "挂载点在线列表"},
        {"MPT:SUB", "挂载点订阅关系"},
        {"USR:STAT", "用户在线状态"},
        {"STR:STAT", "数据流状态"},
        {"STR:ACTIVE", "账号活跃状态"},
        {"ACT:RECORD", "账号记录"},
        {"LOG:MPT", "基站连接日志"},
        {"LOG:USR", "用户连接日志"},
        {"CASTER:NODE", "集群节点"},
        {"CASTER:MASTER", "Master 锁"},
        {"PULL:RECORD", "Pull 转发记录"},
        {"PULL:STAT", "Pull 转发状态"},
        {"PUSH:RECORD", "Push 转发记录"},
        {"PUSH:STAT", "Push 转发状态"},
        {"ALIAS:RULE", "别名规则"},
        {"ACCESS:GROUP", "访问控制组"},
        {"CONF:SERVICE", "服务配置"},
        {"CONF:CORE", "核心配置"},
        {"CONF:AUTH", "认证配置"},
        {"NODE:HISTORY", "节点历史"},
        {"STAT:DAILY", "每日统计缓存"},
        {"MONITOR:REDIS", "Redis 监控历史"},
    };

    // Group keys by prefix (before first : or the full key if no colon)
    struct KeyGroup
    {
        std::string prefix;
        std::string type;
        int count = 0;
        long long fields = 0;     // hlen/llen total
        long long memory = 0;     // bytes
        std::string description;
    };
    std::map<std::string, KeyGroup> groups;

    for (auto &key : keys)
    {
        // Determine prefix: find pattern matching known prefixes, or use first two segments
        std::string prefix;
        for (auto &[kp, desc] : key_descriptions)
        {
            if (key == kp || key.substr(0, kp.size() + 1) == kp + ":")
            {
                prefix = kp;
                break;
            }
        }
        if (prefix.empty())
        {
            // Use first two segments: e.g. "ACCESS:ITEM:xxx" → "ACCESS:ITEM"
            auto pos1 = key.find(':');
            if (pos1 != std::string::npos)
            {
                auto pos2 = key.find(':', pos1 + 1);
                prefix = (pos2 != std::string::npos) ? key.substr(0, pos2) : key;
            }
            else
            {
                prefix = key;
            }
        }

        auto &g = groups[prefix];
        g.prefix = prefix;
        g.count++;

        // Get type, size, memory for each key
        auto t = redis.type(key.c_str());
        g.type = t;
        if (t == "hash")
            g.fields += redis.hlen(key.c_str());
        else if (t == "list")
            g.fields += redis.llen(key.c_str());
        g.memory += redis.memory_usage(key.c_str());

        auto it = key_descriptions.find(prefix);
        if (it != key_descriptions.end())
            g.description = it->second;
    }

    // Build response
    json categories = json::array();
    long long total_memory = 0;
    for (auto &[prefix, g] : groups)
    {
        json cat = {
            {"prefix", g.prefix},
            {"type", g.type},
            {"count", g.count},
            {"fields", g.fields},
            {"memory", g.memory},
            {"description", g.description}};
        categories.push_back(cat);
        total_memory += g.memory;
    }

    json result = {
        {"categories", categories},
        {"total_keys", static_cast<int>(keys.size())},
        {"total_memory", total_memory}};

    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_get_monitor_cluster(const HttpRequest &req, HttpResponse &resp)
{
    auto &redis = sync_redis::instance();

    // Get master node
    auto master_val = redis.get("CASTER:MASTER");
    std::string master_node = master_val.is_string() ? master_val.get<std::string>() : "";

    // Get all nodes
    auto nodes_raw = redis.hgetall(KEY_CASTER_NODE);
    int total_nodes = 0;
    int online_nodes = 0;
    int total_servers = 0;
    int total_clients = 0;
    int total_pull = 0;
    int total_push = 0;
    double total_cpu = 0.0;
    long long total_mem = 0;
    double total_send = 0.0;
    double total_recv = 0.0;
    json nodes_array = json::array();

    for (auto &[uid, node_data] : nodes_raw.items())
    {
        total_nodes++;
        json node_info;
        if (node_data.is_object())
            node_info = node_data;
        else
            continue;

        bool is_master = (uid == master_node);
        int mpt = node_info.value("mpt_count", 0);
        int usr = node_info.value("usr_count", 0);
        int pull = node_info.value("pull_count", 0);
        int push = node_info.value("push_count", 0);
        double cpu = node_info.value("cpu_usage", 0.0);
        long long mem = node_info.value("mem_usage", 0LL);
        double send_s = node_info.value("send_speed", 0.0);
        double recv_s = node_info.value("recv_speed", 0.0);

        online_nodes++;
        total_servers += mpt;
        total_clients += usr;
        total_pull += pull;
        total_push += push;
        total_cpu += cpu;
        total_mem += mem;
        total_send += send_s;
        total_recv += recv_s;

        nodes_array.push_back({
            {"uid", uid},
            {"node_name", node_info.value("node_name", "")},
            {"is_master", is_master},
            {"cpu", cpu},
            {"mem", mem},
            {"mpt", mpt},
            {"usr", usr},
            {"pull", pull},
            {"push", push},
            {"conn", node_info.value("conn_count", 0)},
            {"send_speed", send_s},
            {"recv_speed", recv_s},
            {"send_total", node_info.value("send_total", 0)},
            {"recv_total", node_info.value("recv_total", 0)},
            {"set_version", node_info.value("set_version", "")},
            {"tag_version", node_info.value("tag_version", "")},
            {"queue_delay", node_info.value("queue_delay", 0)}
        });
    }

    // Get Redis latency (simple PING round-trip)
    auto t_start = std::chrono::steady_clock::now();
    redis.get("CASTER:MASTER"); // simple round-trip
    auto t_end = std::chrono::steady_clock::now();
    double latency_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    json result = {
        {"master_node", master_node},
        {"total_nodes", total_nodes},
        {"online_nodes", online_nodes},
        {"total_servers", total_servers},
        {"total_clients", total_clients},
        {"total_pull", total_pull},
        {"total_push", total_push},
        {"total_cpu", total_cpu},
        {"total_mem", total_mem},
        {"total_send_speed", total_send},
        {"total_recv_speed", total_recv},
        {"redis_latency_ms", latency_ms},
        {"nodes", nodes_array}};

    resp.status_code = 200;
    resp.body = result.dump();
}

// ==================== Audit Log Helper ====================

static void audit_log(const HttpRequest &req, const std::string &action,
                      const std::string &target, const json &detail = json::object())
{
    json entry;
    entry["ts"] = time(nullptr);
    entry["user"] = req.headers.count("X-Auth-User") ? req.headers.at("X-Auth-User") : "admin";
    entry["action"] = action;
    entry["target"] = target;
    entry["detail"] = detail;
    entry["ip"] = req.headers.count("X-Forwarded-For") ? req.headers.at("X-Forwarded-For") : "unknown";
    entry["result"] = "ok";
    sync_redis::instance().lpush(KEY_LOG_AUDIT, entry.dump());
    sync_redis::instance().ltrim(KEY_LOG_AUDIT, 0, AUDIT_LOG_MAX - 1);
}

// ==================== Node Config ====================

void http_handler::handle_get_node_config(const HttpRequest &req, HttpResponse &resp)
{
    std::string node_id = get_resource_id(req);
    if (node_id.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing node ID"})";
        return;
    }

    auto &redis = sync_redis::instance();

    // Get node info to verify it exists
    json node_info = redis.hget(KEY_CASTER_NODE, node_id.c_str());
    if (node_info.is_null())
    {
        resp.status_code = 404;
        resp.body = R"({"error":"Node not found"})";
        return;
    }

    // Get running configs from Redis
    json service_conf = redis.get(KEY_CONF_SERVICE);
    json core_conf = redis.get(KEY_CONF_CORE);

    // Build node config response — combine node info with cluster config
    json result = json::object();
    result["node_id"] = node_id;

    // Parse node info
    json node;
    if (node_info.is_string())
    {
        try { node = json::parse(node_info.get<std::string>()); }
        catch (...) { node = node_info; }
    }
    else
    {
        node = node_info;
    }

    result["node_name"] = node.value("node_name", "");
    result["set_version"] = node.value("set_version", "");
    result["tag_version"] = node.value("tag_version", "");

    // Core config (hot-updatable fields)
    json hot_config = json::object();
    if (core_conf.is_object())
    {
        auto &c = core_conf;
        hot_config["update_intv"] = c.value("update_intv", 5);
        hot_config["key_expire_time"] = c.value("key_expire_time", 15);
        hot_config["upload_base_stat"] = c.value("upload_base_stat", true);
        hot_config["upload_rover_stat"] = c.value("upload_rover_stat", true);
        hot_config["base_enable_mult"] = c.value("base_enable_mult", false);
        hot_config["base_keep_early"] = c.value("base_keep_early", true);
        hot_config["rover_enable_mult"] = c.value("rover_enable_mult", false);
        hot_config["rover_keep_early"] = c.value("rover_keep_early", false);
        hot_config["base_notify_inactive"] = c.value("base_notify_inactive", false);
        hot_config["rover_notify_inactive"] = c.value("rover_noify_inactive", false);
    }
    result["core"] = hot_config;

    // Service config
    json svc_config = json::object();
    if (service_conf.is_object())
    {
        if (service_conf.contains("listener"))
        {
            auto &l = service_conf["listener"];
            svc_config["listen_port"] = l.value("listen_port", 2101);
            svc_config["connect_timeout"] = l.value("connect_timeout", 30);
            svc_config["enable_source_login"] = l.value("enable_source_login", true);
            svc_config["enable_server_login"] = l.value("enable_server_login", true);
            svc_config["enable_client_login"] = l.value("enable_client_login", true);
            svc_config["enable_nearest_login"] = l.value("enable_nearest_login", true);
            svc_config["enable_proxy_login"] = l.value("enable_proxy_login", true);
            svc_config["enable_alias_login"] = l.value("enable_alias_login", true);
        }
        if (service_conf.contains("server"))
        {
            auto &s = service_conf["server"];
            svc_config["server_timeout"] = s.value("connect_timeout", 60);
            svc_config["server_heartbeat_interval"] = s.value("heart_beat_interval", 30);
        }
        if (service_conf.contains("client"))
        {
            auto &cl = service_conf["client"];
            svc_config["client_timeout"] = cl.value("connect_timeout", 0);
        }
    }
    result["service"] = svc_config;

    // Config schema — indicates which fields can be hot-updated
    json schema = json::object();
    auto add_schema = [&](const std::string &key, const std::string &label,
                          const std::string &type, bool restart) {
        schema[key] = {{"label", label}, {"type", type}, {"restart_required", restart}};
    };
    add_schema("core.update_intv", "状态上报间隔(秒)", "number", false);
    add_schema("core.key_expire_time", "Key 过期时间(秒)", "number", false);
    add_schema("core.upload_base_stat", "上报基站状态", "boolean", false);
    add_schema("core.upload_rover_stat", "上报用户状态", "boolean", false);
    add_schema("core.base_enable_mult", "基站允许多连接", "boolean", false);
    add_schema("core.base_keep_early", "保留先登录基站", "boolean", false);
    add_schema("core.rover_enable_mult", "用户允许多连接", "boolean", false);
    add_schema("core.rover_keep_early", "保留先登录用户", "boolean", false);
    add_schema("core.base_notify_inactive", "基站离线通知", "boolean", false);
    add_schema("core.rover_notify_inactive", "用户离线通知", "boolean", false);
    add_schema("service.listen_port", "监听端口", "number", true);
    add_schema("service.connect_timeout", "连接超时(秒)", "number", false);
    add_schema("service.enable_source_login", "Source 登录", "boolean", false);
    add_schema("service.enable_server_login", "Server 登录", "boolean", false);
    add_schema("service.enable_client_login", "Client 登录", "boolean", false);
    add_schema("service.enable_nearest_login", "Nearest 登录", "boolean", false);
    add_schema("service.enable_proxy_login", "代理协议", "boolean", false);
    add_schema("service.enable_alias_login", "别名功能", "boolean", false);
    result["schema"] = schema;

    resp.status_code = 200;
    resp.body = result.dump();
}

// ==================== Node Action ====================

void http_handler::handle_post_node_action(const HttpRequest &req, HttpResponse &resp)
{
    std::string node_id = get_resource_id(req);
    if (node_id.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing node ID"})";
        return;
    }

    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }

    std::string action = body.value("action", "");
    if (action.empty())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Missing action"})";
        return;
    }

    // Validate action type
    static const std::unordered_set<std::string> valid_actions = {
        "sync_cluster", "set_log_level",
        "config_update"};

    if (valid_actions.find(action) == valid_actions.end())
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Unknown action"})";
        return;
    }

    auto &redis = sync_redis::instance();

    // Verify node exists
    json node_info = redis.hget(KEY_CASTER_NODE, node_id.c_str());
    if (node_info.is_null())
    {
        resp.status_code = 404;
        resp.body = R"({"error":"Node not found"})";
        return;
    }

    // Build command message
    json cmd;
    cmd["type"] = "action";
    cmd["action"] = action;
    if (body.contains("params"))
        cmd["params"] = body["params"];

    // For config_update, apply changes to Redis config too
    if (action == "config_update" && body.contains("params"))
    {
        auto &params = body["params"];
        std::string section = params.value("section", "");
        std::string key = params.value("key", "");
        auto value = params.value("value", json());

        if (!section.empty() && !key.empty())
        {
            const char *conf_key = nullptr;
            if (section == "core") conf_key = KEY_CONF_CORE;
            else if (section == "service") conf_key = KEY_CONF_SERVICE;

            if (conf_key)
            {
                json conf = redis.get(conf_key);
                if (conf.is_object())
                {
                    conf[key] = value;
                    redis.set(conf_key, conf.dump());
                }
            }

            // Notify all nodes to reload config
            redis.publish("CASTER:CONF", "CONFIG");
        }
    }

    // Publish command to node channel
    std::string channel = "NODE:" + node_id;
    bool published = redis.publish(channel.c_str(), cmd.dump());

    // Audit log
    audit_log(req, "node_action", node_id, {{"action", action}});

    if (published)
    {
        resp.status_code = 200;
        resp.body = json{{"ok", true}, {"node_id", node_id}, {"action", action}}.dump();
    }
    else
    {
        resp.status_code = 500;
        resp.body = R"({"error":"Failed to publish command"})";
    }
}

// ==================== Audit Log ====================

void http_handler::handle_get_audit_logs(const HttpRequest &req, HttpResponse &resp)
{
    auto &redis = sync_redis::instance();

    // Parse query params
    int limit = 100;
    int offset = 0;
    std::string filter_action;
    std::string filter_user;

    auto it = req.query_params.find("limit");
    if (it != req.query_params.end())
    {
        try { limit = std::stoi(it->second); }
        catch (...) {}
        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;
    }
    it = req.query_params.find("offset");
    if (it != req.query_params.end())
    {
        try { offset = std::stoi(it->second); }
        catch (...) {}
        if (offset < 0) offset = 0;
    }
    it = req.query_params.find("action");
    if (it != req.query_params.end()) filter_action = it->second;
    it = req.query_params.find("user");
    if (it != req.query_params.end()) filter_user = it->second;

    // Get all entries in range (get more than needed for filtering)
    int fetch_count = (filter_action.empty() && filter_user.empty()) ? limit : limit * 3;
    json all_entries = redis.lrange(KEY_LOG_AUDIT, offset, offset + fetch_count - 1);

    json filtered = json::array();
    for (auto &entry : all_entries)
    {
        if (!filter_action.empty() && entry.value("action", "") != filter_action)
            continue;
        if (!filter_user.empty() && entry.value("user", "") != filter_user)
            continue;
        filtered.push_back(entry);
        if (static_cast<int>(filtered.size()) >= limit)
            break;
    }

    resp.status_code = 200;
    resp.body = json{{"items", filtered}, {"total", all_entries.size()}}.dump();
}

// Cleanup macros
#undef IMPL_GET_ALL
#undef IMPL_GET_ONE
#undef IMPL_CREATE
#undef IMPL_UPDATE
#undef IMPL_DELETE
