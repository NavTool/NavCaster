#include "http_handler.h"
#include "SysUsage.h"
#include "Caster_Core.h"
#include "account_repository.h"
#include "access_controller.h"
#include "access_repository.h"
#include "alias_controller.h"
#include "alias_repository.h"
#include "broadcast_msg.h"
#include "base64.h"
#include "config_controller.h"
#include "config_repository.h"
#include "redis_keys.h"
#include "ring_log_view.h"
#include "relay_repository.h"
#include "runtime_state_repository.h"
#include "source_controller.h"
#include "source_repository.h"
#include "sse_snapshot_service.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#define __class__ "http_handler"

// Redis key constants — matching CasterWeb and caster_internal
static const char *KEY_ACCOUNT_RECORD = navcaster::redis_keys::ACT_RECORD;
static const char *KEY_ACCOUNT_ACTIVE = navcaster::redis_keys::STR_ACTIVE_LEGACY;
static const char *KEY_SOURCE_RECORD = navcaster::redis_keys::MPT_RECORD;
static const char *KEY_SERVER_STATE = "MPT:STAT";
static const char *KEY_CLIENT_STATE = "USR:STAT";
static const char *KEY_STREAM_STATE = "STR:STAT";
static const char *KEY_ALIAS_RULE = navcaster::redis_keys::ALIAS_RULE;
static const char *KEY_ACCESS_GROUP = navcaster::redis_keys::ACCESS_GROUP;
static const char *KEY_PULL_RECORD = navcaster::redis_keys::PULL_RECORD;
static const char *KEY_PULL_STATE = navcaster::redis_keys::PULL_STAT;
static const char *KEY_PUSH_RECORD = navcaster::redis_keys::PUSH_RECORD;
static const char *KEY_PUSH_STATE = navcaster::redis_keys::PUSH_STAT;
static const char *KEY_CASTER_NODE = "CASTER:NODE";
static const char *KEY_MPT_ONLINE = "MPT:LIST";
static const char *KEY_MPT_SUB = "MPT:SUB";
static const char *KEY_LOG_MPT = "LOG:MPT";
static const char *KEY_LOG_USR = "LOG:USR";

// PRAGMATIC APPROACH: Since all Redis operations are hash operations on local
// Redis with sub-millisecond latency, and the HTTP API is for management only,
// we use synchronous hiredis calls on a dedicated blocking connection.

namespace
{
    // Synchronous Redis helper using a blocking connection
    class sync_redis : public navcaster::storage::RedisHashClient
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
        json hgetall(const char *key) override
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
        json hget(const char *key, const char *field) override
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
        bool hset(const char *key, const char *field, const std::string &value) override
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
        bool hsetnx(const char *key, const char *field, const std::string &value) override
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
        bool hdel(const char *key, const char *field) override
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
        bool set(const char *key, const std::string &value) override
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
        json get(const char *key) override
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
        bool publish(const char *channel, const std::string &message) override
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

        // SCAN prefix* (hash keys only) + HGETALL each → aggregated json object {field: parsed_value, ...}
        json scan_hgetall_prefix(const char *prefix)
        {
            if (!ensure_connected())
                return json::object();

            json result = json::object();
            std::string pattern = std::string(prefix) + "*";
            unsigned long long cursor = 0;
            do
            {
                auto *sreply = static_cast<redisReply *>(
                    redisCommand(_ctx, "SCAN %llu MATCH %s COUNT 200 TYPE hash", cursor, pattern.c_str()));
                if (!sreply)
                {
                    reconnect();
                    break;
                }
                if (sreply->type == REDIS_REPLY_ARRAY && sreply->elements == 2)
                {
                    cursor = std::strtoull(sreply->element[0]->str, nullptr, 10);
                    auto *arr = sreply->element[1];
                    for (size_t i = 0; i < arr->elements; i++)
                    {
                        if (!arr->element[i]->str)
                            continue;
                        const char *hkey = arr->element[i]->str;
                        auto *hreply = static_cast<redisReply *>(redisCommand(_ctx, "HGETALL %s", hkey));
                        if (hreply && hreply->type == REDIS_REPLY_ARRAY)
                        {
                            for (size_t j = 0; j + 1 < hreply->elements; j += 2)
                            {
                                std::string field = hreply->element[j]->str ? hreply->element[j]->str : "";
                                std::string value = hreply->element[j + 1]->str ? hreply->element[j + 1]->str : "";
                                try { result[field] = json::parse(value); }
                                catch (...) { result[field] = value; }
                            }
                        }
                        if (hreply)
                            freeReplyObject(hreply);
                    }
                }
                else
                {
                    freeReplyObject(sreply);
                    break;
                }
                freeReplyObject(sreply);
            } while (cursor != 0);
            return result;
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

        // INCR key → new value
        long long incr(const char *key)
        {
            if (!ensure_connected())
                return 0;
            auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "INCR %s", key));
            long long v = 0;
            if (reply && reply->type == REDIS_REPLY_INTEGER) v = reply->integer;
            if (reply) freeReplyObject(reply); else reconnect();
            return v;
        }

        // LPUSH key value → new length
        long long lpush(const char *key, const std::string &value)
        {
            if (!ensure_connected())
                return 0;
            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "LPUSH %s %s", key, value.c_str()));
            long long len = 0;
            if (reply && reply->type == REDIS_REPLY_INTEGER) len = reply->integer;
            if (reply) freeReplyObject(reply); else reconnect();
            return len;
        }

        // LTRIM key start stop
        bool ltrim(const char *key, long long start, long long stop)
        {
            if (!ensure_connected())
                return false;
            auto *reply = static_cast<redisReply *>(
                redisCommand(_ctx, "LTRIM %s %lld %lld", key, start, stop));
            bool ok = reply && reply->type != REDIS_REPLY_ERROR;
            if (reply) freeReplyObject(reply); else reconnect();
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

    std::int64_t current_unix_seconds()
    {
        return static_cast<std::int64_t>(std::time(nullptr));
    }

    void write_account_repository_error(const navcaster::storage::AccountRepositoryResult &result, HttpResponse &resp)
    {
        switch (result.status)
        {
        case navcaster::storage::RepositoryStatus::Invalid:
            resp.status_code = 400;
            break;
        case navcaster::storage::RepositoryStatus::NotFound:
            resp.status_code = 404;
            break;
        case navcaster::storage::RepositoryStatus::Conflict:
            resp.status_code = 409;
            break;
        case navcaster::storage::RepositoryStatus::RedisError:
            resp.status_code = 500;
            break;
        case navcaster::storage::RepositoryStatus::Ok:
            resp.status_code = 200;
            break;
        }
        resp.body = json{{"error", result.error.empty() ? "Repository error" : result.error}}.dump();
    }

    void write_repository_error(navcaster::storage::RepositoryStatus status, const std::string &error, HttpResponse &resp)
    {
        switch (status)
        {
        case navcaster::storage::RepositoryStatus::Invalid:
            resp.status_code = 400;
            break;
        case navcaster::storage::RepositoryStatus::NotFound:
            resp.status_code = 404;
            break;
        case navcaster::storage::RepositoryStatus::Conflict:
            resp.status_code = 409;
            break;
        case navcaster::storage::RepositoryStatus::RedisError:
            resp.status_code = 500;
            break;
        case navcaster::storage::RepositoryStatus::Ok:
            resp.status_code = 200;
            break;
        }
        resp.body = json{{"error", error.empty() ? "Repository error" : error}}.dump();
    }

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
        navcaster::storage::AccessRepository repo(sync_redis::instance());
        repo.ensure_builtin_groups(current_unix_seconds());
        spdlog::info("[{}:{}]: Ensured default access group exists", __class__, __func__);
    }

    // Configure server
    _server.set_cors_origin(config.cors_origin);
    _server.add_public_path("/api/auth/login");
    _server.add_public_path("/api/status/health");
    _server.set_auth_validator([this](const std::string &token) -> bool
                               { return validate_token(token); });
    _server.set_actor_resolver([this](const std::string &token) -> std::string
                               { return lookup_user(token); });
    _server.set_audit_sink([this](const HttpRequest &req, const HttpResponse &resp,
                                  const std::string &actor, const std::string &client_ip)
                           { write_audit(req, resp, actor, client_ip); });

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
    _server.route(EVHTTP_REQ_POST, "/api/servers/kick/*", [this](auto &req, auto &resp)
                  { handle_kick_server(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/servers/*", [this](auto &req, auto &resp)
                  { handle_get_server(req, resp); });

    // ==================== Clients (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/clients", [this](auto &req, auto &resp)
                  { handle_get_clients(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/clients/kick/*", [this](auto &req, auto &resp)
                  { handle_kick_client(req, resp); });
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
    _server.route(EVHTTP_REQ_GET, "/api/monitor/redis/history", [this](auto &req, auto &resp)
                  { handle_get_monitor_redis_history(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/monitor/cluster", [this](auto &req, auto &resp)
                  { handle_get_monitor_cluster(req, resp); });

    // ==================== V3 运维接口 ====================
    _server.route(EVHTTP_REQ_GET, "/api/audit", [this](auto &req, auto &resp)
                  { handle_get_audit(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/logs/ring", [this](auto &req, auto &resp)
                  { handle_get_logs_ring(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/system/events", [this](auto &req, auto &resp)
                  { handle_get_system_events(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/nodes/log-level/*", [this](auto &req, auto &resp)
                  { handle_set_node_log_level(req, resp); });

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
    _sse.set_max_clients(200); // V3 C3: 限制 SSE 并发连接

    // V3 B4: Redis 状态采样定时器 (60s 周期，保留 24h = 1440 点)
    _redis_sample_timer = event_new(base, -1, EV_PERSIST, on_redis_sample_timer, this);
    if (_redis_sample_timer)
    {
        struct timeval tv {60, 0};
        event_add(_redis_sample_timer, &tv);
        spdlog::info("[{}:{}]: Redis history sampling enabled (60s interval)", __class__, __func__);
    }

    // Register SSE channels through repository-backed snapshot service.
    static navcaster::http_api::SseSnapshotService sse_snapshots(
        sync_redis::instance(),
        sync_redis_auth::instance().redis());
    sse_snapshots.register_channels(_sse);

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

std::string http_handler::generate_token(const std::string &user)
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
    _active_tokens[token] = user;
    return token;
}

bool http_handler::validate_token(const std::string &token)
{
    if (token.empty())
        return false;
    std::lock_guard<std::mutex> lock(_token_mutex);
    return _active_tokens.count(token) > 0;
}

std::string http_handler::lookup_user(const std::string &token)
{
    if (token.empty()) return {};
    std::lock_guard<std::mutex> lock(_token_mutex);
    auto it = _active_tokens.find(token);
    return it == _active_tokens.end() ? std::string{} : it->second;
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
        std::string token = generate_token(user);
        json result = {{"token", token}, {"username", user}};
        resp.status_code = 200;
        resp.body = result.dump();
        spdlog::info("[{}:{}]: Login success, user: {}", __class__, __func__, user);
    }
    else
    {
        // Also check Redis-stored auth config (allows password change at runtime)
        navcaster::storage::ConfigRepository config_repo(sync_redis::instance());
        json auth_conf = config_repo.get_config(navcaster::storage::ConfigSection::Auth);
        std::string redis_user = auth_conf.is_object() ? auth_conf.value("admin_user", "") : "";
        std::string redis_pass = auth_conf.is_object() ? auth_conf.value("admin_password", "") : "";
        if (!redis_user.empty() && user == redis_user && pass == redis_pass)
        {
            std::string token = generate_token(user);
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

// ==================== Accounts (ACT:RECORD) — uses auth redis ====================

void http_handler::handle_get_accounts(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::AccountRepository repo(sync_redis_auth::instance().redis());
    json data = repo.list_accounts();
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
    navcaster::storage::AccountRepository repo(sync_redis_auth::instance().redis());
    json data = repo.get_account(id);
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
    navcaster::storage::AccountRepository repo(sync_redis_auth::instance().redis());
    auto result = repo.create_account(std::move(body), current_unix_seconds());
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_account_repository_error(result, resp);
        return;
    }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"account", result.account}}.dump();
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
    navcaster::storage::AccountRepository repo(sync_redis_auth::instance().redis());
    auto result = repo.update_account(id, std::move(body), current_unix_seconds());
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_account_repository_error(result, resp);
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
    navcaster::storage::AccountRepository repo(sync_redis_auth::instance().redis());
    auto result = repo.delete_account(id);
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_account_repository_error(result, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_get_account_actives(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::AccountRepository repo(sync_redis_auth::instance().redis());
    json data = repo.list_legacy_active_sessions();
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Sources (MPT:RECORD) ====================

void http_handler::handle_get_sources(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.list_sources();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.get_source(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.create_source(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.update_source(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.delete_source(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Servers (MPT:STAT) read-only ====================

void http_handler::handle_get_servers(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.list(navcaster::storage::RuntimeStateKind::Server);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_server(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.get(navcaster::storage::RuntimeStateKind::Server, id);
    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Clients (USR:STAT) read-only ====================

void http_handler::handle_get_clients(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.list(navcaster::storage::RuntimeStateKind::Client);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_client(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.get(navcaster::storage::RuntimeStateKind::Client, id);
    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Force Offline (Kick) ====================

static int publish_kick_broadcast(const std::string &uid, bool is_server, const std::string &reason)
{
    broadcast_msg item;
    item.type = is_server ? caster::core::BOARDCAST_TYPE_SERVER_OPERATE
                          : caster::core::BOARDCAST_TYPE_CLIENT_OPERATE;
    item.operate = caster::core::BOARDCAST_OPERATE_DELETE;
    item.target = uid;
    item.msg_str = "";
    item.reason_str = reason;
    return sync_redis::instance().publish("CASTER:BROADCAST", item.toString()) ? 0 : 1;
}

void http_handler::handle_kick_server(const HttpRequest &req, HttpResponse &resp)
{
    // URL: /api/servers/kick/{uid}
    std::string uid = get_resource_id(req);
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing UID"})"; return; }

    auto val = sync_redis::instance().hget(KEY_SERVER_STATE, uid.c_str());
    if (val.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Server not found"})"; return; }

    int rc = publish_kick_broadcast(uid, true, "Force offline by administrator");
    if (rc != 0) { resp.status_code = 500; resp.body = R"({"error":"Failed to publish kick broadcast"})"; return; }

    resp.status_code = 200;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

void http_handler::handle_kick_client(const HttpRequest &req, HttpResponse &resp)
{
    // URL: /api/clients/kick/{uid}
    std::string uid = get_resource_id(req);
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing UID"})"; return; }

    auto val = sync_redis::instance().hget(KEY_CLIENT_STATE, uid.c_str());
    if (val.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Client not found"})"; return; }

    int rc = publish_kick_broadcast(uid, false, "Force offline by administrator");
    if (rc != 0) { resp.status_code = 500; resp.body = R"({"error":"Failed to publish kick broadcast"})"; return; }

    resp.status_code = 200;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

// ==================== Streams (STR:STAT) read-only ====================

void http_handler::handle_get_streams(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.list(navcaster::storage::RuntimeStateKind::Stream);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_stream(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.get(navcaster::storage::RuntimeStateKind::Stream, id);
    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Aliases (ALIAS:RULE) ====================

void http_handler::handle_get_aliases(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.list_aliases();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.get_alias(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.create_alias(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.update_alias(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.delete_alias(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Access Groups (ACCESS:GROUP) ====================

void http_handler::handle_get_access_groups(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.list_groups();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.get_group(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.create_group(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.update_group(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.delete_group(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Access Items (ACCESS:ITEM:<group_uid>) ====================

void http_handler::handle_get_access_items(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.list_items(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_access_item(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.create_item(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_access_item(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.update_item(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_access_item(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(sync_redis::instance(), current_unix_seconds());
    auto result = controller.delete_item(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Pull Relays (PULL:RECORD / PULL:STAT) ====================

void http_handler::handle_get_pulls(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    json data = repo.list_records(navcaster::storage::RelayKind::Pull);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_pull(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    json data = repo.get_record(navcaster::storage::RelayKind::Pull, id);
    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_create_pull(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.create_record(navcaster::storage::RelayKind::Pull, std::move(body));
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", result.uid}}.dump();
}

void http_handler::handle_update_pull(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.update_record(navcaster::storage::RelayKind::Pull, id, std::move(body));
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_pull(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.delete_record(navcaster::storage::RelayKind::Pull, id);
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_get_pull_states(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    json data = repo.list_states(navcaster::storage::RelayKind::Pull);
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Push Relays (PUSH:RECORD / PUSH:STAT) ====================

void http_handler::handle_get_pushs(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    json data = repo.list_records(navcaster::storage::RelayKind::Push);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_push(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    json data = repo.get_record(navcaster::storage::RelayKind::Push, id);
    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_create_push(const HttpRequest &req, HttpResponse &resp)
{
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.create_record(navcaster::storage::RelayKind::Push, std::move(body));
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 201;
    resp.body = json{{"ok", true}, {"uid", result.uid}}.dump();
}

void http_handler::handle_update_push(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.update_record(navcaster::storage::RelayKind::Push, id, std::move(body));
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_delete_push(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.delete_record(navcaster::storage::RelayKind::Push, id);
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}}.dump();
}

void http_handler::handle_get_push_states(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    json data = repo.list_states(navcaster::storage::RelayKind::Push);
    resp.status_code = 200;
    resp.body = data.dump();
}

// ==================== Relay Start/Stop ====================

void http_handler::handle_relay_start(const HttpRequest &req, HttpResponse &resp)
{
    // URL: /api/relays/{pull|push}/start/{uid}
    std::string path = req.path;
    std::string uid = get_resource_id(req);
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }

    const auto kind = path.find("/pull/") != std::string::npos
                          ? navcaster::storage::RelayKind::Pull
                          : navcaster::storage::RelayKind::Push;
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.set_enabled(kind, uid, true);
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

void http_handler::handle_relay_stop(const HttpRequest &req, HttpResponse &resp)
{
    // URL: /api/relays/{pull|push}/stop/{uid}
    std::string path = req.path;
    std::string uid = get_resource_id(req);
    if (uid.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }

    const auto kind = path.find("/pull/") != std::string::npos
                          ? navcaster::storage::RelayKind::Pull
                          : navcaster::storage::RelayKind::Push;
    navcaster::storage::RelayRepository repo(sync_redis::instance());
    auto result = repo.set_enabled(kind, uid, false);
    if (result.status != navcaster::storage::RepositoryStatus::Ok)
    {
        write_repository_error(result.status, result.error, resp);
        return;
    }
    resp.status_code = 200;
    resp.body = json{{"ok", true}, {"uid", uid}}.dump();
}

// ==================== Nodes (CASTER:NODE) read-only ====================

void http_handler::handle_get_nodes(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.list(navcaster::storage::RuntimeStateKind::Node);
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_node(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing ID"})"; return; }
    navcaster::storage::RuntimeStateRepository repo(sync_redis::instance());
    json data = repo.get(navcaster::storage::RuntimeStateKind::Node, id);
    if (data.is_null()) { resp.status_code = 404; resp.body = R"({"error":"Not found"})"; return; }
    resp.status_code = 200;
    resp.body = data.dump();
}

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
        max_limit = 8640;
    }
    else if (range == "1m")
    {
        key = "NODE:HISTORY:" + node_id + ":1M";
        max_limit = 43200;
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
// LOG:MPT 与 LOG:USR 是 hash key 的前缀，实际数据在 LOG:MPT:<mount> / LOG:USR:<user>
// 这里需要 SCAN+HGETALL 聚合，而不是直接 HGETALL

void http_handler::handle_get_server_logs(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    json data = sync_redis::instance().scan_hgetall_prefix("LOG:MPT:");
    resp.status_code = 200;
    resp.body = data.dump();
}

void http_handler::handle_get_client_logs(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    json data = sync_redis::instance().scan_hgetall_prefix("LOG:USR:");
    resp.status_code = 200;
    resp.body = data.dump();
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
    json mpt_logs = redis.scan_hgetall_prefix("LOG:MPT:");
    json usr_logs = redis.scan_hgetall_prefix("LOG:USR:");

    // PULL=5, PUSH=6 (CasterRegisterType enum values)
    static constexpr int TYPE_PULL = 5;
    static constexpr int TYPE_PUSH = 6;

    // Aggregate
    long long mpt_connections = 0, usr_connections = 0;
    long long pull_connections = 0, push_connections = 0;
    long long total_duration_mpt = 0, total_duration_usr = 0;
    int peak_concurrent_mpt = 0, peak_concurrent_usr = 0;
    int peak_concurrent_pull = 0, peak_concurrent_push = 0;
    std::set<std::string> unique_mounts, unique_users;

    // Time-slot concurrency: adapt bucket size to range
    //  - <= 48h : 1h buckets
    //  - <= 31d : 1d buckets
    //  - else   : weekly buckets, capped at 200 buckets
    long long range_secs = std::max(1LL, end_ts - start_ts);
    long long bucket_secs;
    if (range_secs <= 48LL * 3600LL) bucket_secs = 3600LL;
    else if (range_secs <= 31LL * 86400LL) bucket_secs = 86400LL;
    else bucket_secs = 7LL * 86400LL;
    int num_hours = static_cast<int>((range_secs + bucket_secs - 1) / bucket_secs);
    if (num_hours <= 0) num_hours = 1;
    if (num_hours > 200) num_hours = 200;
    std::vector<int> mpt_hourly(num_hours, 0);
    std::vector<int> usr_hourly(num_hours, 0);
    std::vector<int> pull_hourly(num_hours, 0);
    std::vector<int> push_hourly(num_hours, 0);

    auto process_logs = [&](const json &logs, bool is_mpt)
    {
        for (auto &[field, entry] : logs.items())
        {
            if (!entry.is_object()) continue;
            int type_val = entry.value("type", 0);
            bool is_pull = is_mpt && (type_val == TYPE_PULL);
            bool is_push = !is_mpt && (type_val == TYPE_PUSH);

            long long ct = entry.value("connect_time", 0LL);
            long long dt = entry.value("disconnect_time", 0LL);
            if (dt == 0) dt = now_ts; // still online

            // Skip if completely outside range
            if (dt < start_ts || ct >= end_ts) continue;

            long long overlap_start = std::max(ct, start_ts);
            long long overlap_end = std::min(dt, end_ts);

            if (is_pull)
            {
                pull_connections++;
            }
            else if (is_push)
            {
                push_connections++;
            }
            else if (is_mpt)
            {
                mpt_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_mounts.insert(name);
                total_duration_mpt += (overlap_end - overlap_start);
            }
            else
            {
                usr_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_users.insert(name);
                total_duration_usr += (overlap_end - overlap_start);
            }

            // Bucket concurrency: mark each bucket this session overlaps
            long long h_start = std::max(ct, start_ts);
            long long h_end = std::min(dt, end_ts);
            int bucket_begin = static_cast<int>((h_start - start_ts) / bucket_secs);
            int bucket_end = static_cast<int>((h_end - start_ts) / bucket_secs);
            if (bucket_begin < 0) bucket_begin = 0;
            if (bucket_end >= num_hours) bucket_end = num_hours - 1;
            if (bucket_end < bucket_begin) continue;
            std::vector<int> *hourly_ptr = is_pull ? &pull_hourly
                                         : is_push ? &push_hourly
                                         : is_mpt  ? &mpt_hourly
                                                   : &usr_hourly;
            for (int b = bucket_begin; b <= bucket_end; b++)
                (*hourly_ptr)[b]++;
        }
    };

    process_logs(mpt_logs, true);
    process_logs(usr_logs, false);

    for (int i = 0; i < num_hours; i++)
    {
        if (mpt_hourly[i] > peak_concurrent_mpt) peak_concurrent_mpt = mpt_hourly[i];
        if (usr_hourly[i] > peak_concurrent_usr) peak_concurrent_usr = usr_hourly[i];
        if (pull_hourly[i] > peak_concurrent_pull) peak_concurrent_pull = pull_hourly[i];
        if (push_hourly[i] > peak_concurrent_push) peak_concurrent_push = push_hourly[i];
    }

    // Build trend
    json hourly_trend = json::array();
    for (int i = 0; i < num_hours; i++)
    {
        json h;
        h["ts"] = start_ts + i * bucket_secs;
        h["mpt"] = mpt_hourly[i];
        h["usr"] = usr_hourly[i];
        h["pull"] = pull_hourly[i];
        h["push"] = push_hourly[i];
        hourly_trend.push_back(h);
    }

    json result;
    result["start"] = start_ts;
    result["end"] = end_ts;
    result["mpt_connections"] = mpt_connections;
    result["usr_connections"] = usr_connections;
    result["pull_connections"] = pull_connections;
    result["push_connections"] = push_connections;
    result["peak_concurrent_mpt"] = peak_concurrent_mpt;
    result["peak_concurrent_usr"] = peak_concurrent_usr;
    result["peak_concurrent_pull"] = peak_concurrent_pull;
    result["peak_concurrent_push"] = peak_concurrent_push;
    result["avg_duration_mpt"] = mpt_connections > 0 ? total_duration_mpt / mpt_connections : 0;
    result["avg_duration_usr"] = usr_connections > 0 ? total_duration_usr / usr_connections : 0;
    result["unique_mountpoints"] = static_cast<int>(unique_mounts.size());
    result["unique_users"] = static_cast<int>(unique_users.size());
    result["hourly_trend"] = hourly_trend;
    result["bucket_seconds"] = bucket_secs;

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
    json mpt_logs = redis.scan_hgetall_prefix("LOG:MPT:");
    json usr_logs = redis.scan_hgetall_prefix("LOG:USR:");

    // PULL=5, PUSH=6 (CasterRegisterType enum values)
    static constexpr int TYPE_PULL_D = 5;
    static constexpr int TYPE_PUSH_D = 6;

    long long mpt_connections = 0, usr_connections = 0;
    long long pull_connections = 0, push_connections = 0;
    long long total_duration_mpt = 0, total_duration_usr = 0;
    int peak_concurrent_mpt = 0, peak_concurrent_usr = 0;
    int peak_concurrent_pull = 0, peak_concurrent_push = 0;
    std::set<std::string> unique_mounts, unique_users;

    int num_hours = 24;
    std::vector<int> mpt_hourly(num_hours, 0);
    std::vector<int> usr_hourly(num_hours, 0);
    std::vector<int> pull_hourly(num_hours, 0);
    std::vector<int> push_hourly(num_hours, 0);

    auto process_logs = [&](const json &logs, bool is_mpt)
    {
        for (auto &[field, entry] : logs.items())
        {
            if (!entry.is_object()) continue;
            int type_val = entry.value("type", 0);
            bool is_pull = is_mpt && (type_val == TYPE_PULL_D);
            bool is_push = !is_mpt && (type_val == TYPE_PUSH_D);

            long long ct = entry.value("connect_time", 0LL);
            long long dt = entry.value("disconnect_time", 0LL);
            if (dt == 0) dt = now_ts;
            if (dt < start_ts || ct >= end_ts) continue;

            long long overlap_start = std::max(ct, start_ts);
            long long overlap_end = std::min(dt, end_ts);

            if (is_pull)
            {
                pull_connections++;
            }
            else if (is_push)
            {
                push_connections++;
            }
            else if (is_mpt)
            {
                mpt_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_mounts.insert(name);
                total_duration_mpt += (overlap_end - overlap_start);
            }
            else
            {
                usr_connections++;
                std::string name = entry.value("name", "");
                if (!name.empty()) unique_users.insert(name);
                total_duration_usr += (overlap_end - overlap_start);
            }

            long long h_start = std::max(ct, start_ts);
            long long h_end = std::min(dt, end_ts);
            int bucket_begin = std::max(0, static_cast<int>((h_start - start_ts) / 3600));
            int bucket_end = std::min(num_hours - 1, static_cast<int>((h_end - start_ts) / 3600));
            std::vector<int> *hourly_ptr = is_pull ? &pull_hourly
                                         : is_push ? &push_hourly
                                         : is_mpt  ? &mpt_hourly
                                                   : &usr_hourly;
            for (int b = bucket_begin; b <= bucket_end; b++)
                (*hourly_ptr)[b]++;
        }
    };

    process_logs(mpt_logs, true);
    process_logs(usr_logs, false);

    for (int i = 0; i < num_hours; i++)
    {
        if (mpt_hourly[i] > peak_concurrent_mpt) peak_concurrent_mpt = mpt_hourly[i];
        if (usr_hourly[i] > peak_concurrent_usr) peak_concurrent_usr = usr_hourly[i];
        if (pull_hourly[i] > peak_concurrent_pull) peak_concurrent_pull = pull_hourly[i];
        if (push_hourly[i] > peak_concurrent_push) peak_concurrent_push = push_hourly[i];
    }

    json hourly_trend = json::array();
    for (int i = 0; i < num_hours; i++)
    {
        json h;
        h["ts"] = start_ts + i * 3600;
        h["mpt"] = mpt_hourly[i];
        h["usr"] = usr_hourly[i];
        h["pull"] = pull_hourly[i];
        h["push"] = push_hourly[i];
        hourly_trend.push_back(h);
    }

    json result;
    result["date"] = date_str;
    result["start"] = start_ts;
    result["end"] = end_ts;
    result["mpt_connections"] = mpt_connections;
    result["usr_connections"] = usr_connections;
    result["pull_connections"] = pull_connections;
    result["push_connections"] = push_connections;
    result["peak_concurrent_mpt"] = peak_concurrent_mpt;
    result["peak_concurrent_usr"] = peak_concurrent_usr;
    result["peak_concurrent_pull"] = peak_concurrent_pull;
    result["peak_concurrent_push"] = peak_concurrent_push;
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

    json mpt_logs = sync_redis::instance().scan_hgetall_prefix("LOG:MPT:");

    // 按挂载点/资源名聚合：包含普通基站、PULL 中继、别名、最近点等
    struct MptStat { long long total_duration = 0; int connections = 0; long long last_seen = 0; int type_mask = 0; };
    std::map<std::string, MptStat> stats;

    for (auto &[field, entry] : mpt_logs.items())
    {
        if (!entry.is_object()) continue;
        // 保留 PULL（type=5），同时记录类型位图
        int t = entry.value("type", 0);
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
        if (t > 0 && t < 31) s.type_mask |= (1 << t);
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
        // 类型标签数组：SERVER/PULL 等
        json types = json::array();
        if (s.type_mask & (1 << 1)) types.push_back("SERVER");
        if (s.type_mask & (1 << 5)) types.push_back("PULL");
        item["types"] = types;
        result.push_back(item);
        count++;
    }

    resp.status_code = 200;
    resp.body = result.dump();
}
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

    json usr_logs = sync_redis::instance().scan_hgetall_prefix("LOG:USR:");

    struct UsrStat { long long total_duration = 0; int connections = 0; long long last_seen = 0; std::set<std::string> mounts; int type_mask = 0; };
    std::map<std::string, UsrStat> stats;

    for (auto &[field, entry] : usr_logs.items())
    {
        if (!entry.is_object()) continue;
        // 保留 PUSH（type=6），只记录类型位图
        int t = entry.value("type", 0);
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
        if (t > 0 && t < 31) s.type_mask |= (1 << t);
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
        json types = json::array();
        if (s.type_mask & (1 << 2)) types.push_back("CLIENT");
        if (s.type_mask & (1 << 3)) types.push_back("NEAREST");
        if (s.type_mask & (1 << 4)) types.push_back("ALIAS");
        if (s.type_mask & (1 << 6)) types.push_back("PUSH");
        item["types"] = types;
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

    std::string log_key = "LOG:MPT:" + mount;
    json mpt_logs = sync_redis::instance().hgetall(log_key.c_str());
    long long now_ts = static_cast<long long>(time(nullptr));

    json result = json::array();
    for (auto &[field, entry] : mpt_logs.items())
    {
        if (!entry.is_object()) continue;

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

    std::string log_key = "LOG:USR:" + user;
    json usr_logs = sync_redis::instance().hgetall(log_key.c_str());
    long long now_ts = static_cast<long long>(time(nullptr));

    json result = json::array();
    for (auto &[field, entry] : usr_logs.items())
    {
        if (!entry.is_object()) continue;

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

    // SSE / runtime
    status["sse_clients"] = static_cast<unsigned long long>(_sse.client_count());
    status["sse_max_clients"] = static_cast<unsigned long long>(_sse.max_clients());
    status["node_id"] = CASTER::Get_Node_ID();
    status["log_level"] = spdlog::level::to_string_view(spdlog::get_level()).data();

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
    navcaster::http_api::ConfigController controller(
        sync_redis::instance(),
        {_config.admin_user, _config.admin_password});
    controller.save_config(section, json_str);
}

void http_handler::handle_get_configs(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::ConfigController controller(
        sync_redis::instance(),
        {_config.admin_user, _config.admin_password});
    auto result = controller.get_configs();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_config(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::ConfigController controller(
        sync_redis::instance(),
        {_config.admin_user, _config.admin_password});
    auto result = controller.get_config(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_config(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::ConfigController controller(
        sync_redis::instance(),
        {_config.admin_user, _config.admin_password});
    auto result = controller.update_config(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
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
        // 挂载点 / 基站
        {"MPT:STAT", "基站/挂载点实时状态"},
        {"MPT:RECORD", "自定义挂载点源列表记录"},
        {"MPT:SOURCE", "挂载点源列表（自动解析）"},
        {"MPT:LIST", "挂载点在线列表（name→connect_key）"},
        {"MPT:REC", "挂载点连接列表（connect_key→登录时间）"},
        {"MPT:SUB", "挂载点订阅关系（name→订阅者列表）"},
        {"MPT:GEO", "挂载点位置（经纬度）"},

        // 用户 / 移动站
        {"USR:STAT", "用户实时状态（流量/客户端信息）"},
        {"USR:LIST", "用户在线列表"},
        {"USR:REC", "用户连接列表"},
        {"USR:SUB", "用户数据订阅列表"},
        {"USR:GEO", "用户最近位置"},

        // 数据流
        {"STR:STAT", "数据流状态（含基站和用户所有连接）"},
        {"STR:ACTIVE", "账号活跃会话"},

        // 账号 / 权限
        {"ACT:RECORD", "账号记录（配置）"},
        {"ALIAS:RULE", "挂载点别名规则"},
        {"ACCESS:GROUP", "访问控制组"},
        {"ACCESS:ITEM", "访问控制组成员项"},

        // 日志 / 审计
        {"LOG:MPT", "基站/挂载点连接历史 (按 mount 分 hash)"},
        {"LOG:USR", "用户连接历史 (按 user 分 hash)"},
        {"LOG:NODE", "节点上下线事件"},
        {"LOG:AUDIT", "HTTP API 审计日志 (list)"},

        // 集群 / 节点
        {"CASTER:NODE", "集群节点状态"},
        {"CASTER:MASTER", "Master 节点锁"},
        {"NODE:HISTORY", "节点历史时间序列（5s/1m/5m）"},

        // 转发
        {"PULL:RECORD", "Pull 数据拉取配置"},
        {"PULL:STAT", "Pull 转发运行状态"},
        {"PUSH:RECORD", "Push 数据推送配置"},
        {"PUSH:STAT", "Push 转发运行状态"},

        // 配置
        {"CONF:SERVICE", "service 层配置快照"},
        {"CONF:CORE", "core 层配置快照"},
        {"CONF:AUTH", "auth 层配置快照"},

        // 统计 / 监控
        {"STAT:DAILY", "按天统计缓存 (7 天 TTL)"},
        {"MONITOR:REDIS", "Redis 监控历史 (每 60s 采样)"},
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

    // 全局推/拉统计 (跨节点合计，仅统计 state==1 的有效连接)
    int total_pull = 0;
    int total_push = 0;
    std::unordered_map<std::string, int> pull_by_node;
    std::unordered_map<std::string, int> push_by_node;
    auto collect_running_relays = [](const json &states, std::unordered_map<std::string, int> &by_node) {
        int total = 0;
        if (!states.is_object())
        {
            return total;
        }
        for (const auto &[uid, value] : states.items())
        {
            json item = value;
            if (item.is_string())
            {
                try { item = json::parse(item.get<std::string>()); }
                catch (...) { continue; }
            }
            if (!item.is_object() || item.value("state", 0) != 1)
            {
                continue;
            }
            total++;
            std::string node_uid = item.value("node_uid", std::string());
            if (!node_uid.empty())
            {
                by_node[node_uid]++;
            }
        }
        return total;
    };

    total_pull = collect_running_relays(redis.hgetall("PULL:STAT"), pull_by_node);
    total_push = collect_running_relays(redis.hgetall("PUSH:STAT"), push_by_node);

    // Get all nodes
    auto nodes_raw = redis.hgetall(KEY_CASTER_NODE);
    int total_nodes = 0;
    int online_nodes = 0;
    int total_servers = 0;
    int total_clients = 0;
    double total_cpu = 0.0;
    double total_mem = 0.0;
    double total_send = 0.0;
    double total_recv = 0.0;
    long long now_ts = static_cast<long long>(std::time(nullptr));
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
        // proto JSON 使用 snake_case (preserve_proto_field_names=true)
        int mpt = node_info.value("server_count", 0);
        int usr = node_info.value("client_count", 0);
        int pull = pull_by_node[uid];
        int push = push_by_node[uid];
        int conn = node_info.value("connect_count", 0);
        double cpu = node_info.value("cpu_usage", 0.0);
        double mem = node_info.value("mem_usage", 0.0);
        double send_s = node_info.value("send_speed", 0.0);
        double recv_s = node_info.value("recv_speed", 0.0);
        long long send_t = node_info.value("send_total", 0LL);
        long long recv_t = node_info.value("recv_total", 0LL);
        long long online_time = node_info.value("online_time", 0LL);
        long long update_time = node_info.value("update_time", 0LL);
        long long uptime_sec = (online_time > 0) ? (now_ts - online_time) : 0;

        // 节点心跳判定: update_time 超过 60s 视为掉线
        bool online = (update_time == 0) || (now_ts - update_time < 60);
        if (online)
        {
            online_nodes++;
            total_servers += mpt;
            total_clients += usr;
            total_cpu += cpu;
            total_mem += mem;
            total_send += send_s;
            total_recv += recv_s;
        }

        nodes_array.push_back({
            {"uid", uid},
            {"node_name", node_info.value("node_name", "")},
            {"is_master", is_master},
            {"online", online},
            {"cpu", cpu},
            {"mem", mem},
            {"mpt", mpt},
            {"usr", usr},
            {"pull", pull},
            {"push", push},
            {"conn", conn},
            {"send_speed", send_s},
            {"recv_speed", recv_s},
            {"send_total", send_t},
            {"recv_total", recv_t},
            {"set_version", node_info.value("set_version", "")},
            {"tag_version", node_info.value("tag_version", "")},
            {"queue_delay", node_info.value("queue_delay", 0)},
            {"hostname", node_info.value("hostname", "")},
            {"listen_port", node_info.value("listen_port", 0)},
            {"http_port", node_info.value("http_port", 0)},
            {"process_id", node_info.value("process_id", 0LL)},
            {"http_enabled", node_info.value("http_enabled", false)},
            {"online_time", online_time},
            {"update_time", update_time},
            {"uptime_sec", uptime_sec},
            {"pub_ping_delay", node_info.value("pub_ping_delay", 0LL)},
            {"sub_ping_delay", node_info.value("sub_ping_delay", 0LL)}
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

// ==================== V3 \u5ba1\u8ba1 / \u73af\u5f62\u65e5\u5fd7 / Redis \u91c7\u6837 / \u8282\u70b9\u4e8b\u4ef6 / \u52a8\u6001\u65e5\u5fd7\u7ea7\u522b ====================

namespace
{
    static const char *KEY_AUDIT_LOG = "LOG:AUDIT";
    static const char *KEY_AUDIT_SEQ = "LOG:AUDIT:SEQ";
    static const int   AUDIT_KEEP    = 50000;
    static const char *KEY_REDIS_HISTORY = "MONITOR:REDIS:HISTORY";
    static const int   REDIS_HISTORY_KEEP = 10080; // 7d * 24h * 60min

    // Mask sensitive fields in a JSON body (in-place).
    void mask_secrets(json &j)
    {
        if (!j.is_object()) return;
        for (auto &[k, v] : j.items())
        {
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
            if (lk == "password" || lk == "token" || lk == "secret" || lk == "admin_password")
            {
                if (v.is_string()) v = "***";
            }
            else if (v.is_object())
            {
                mask_secrets(v);
            }
        }
    }

    std::string method_str(evhttp_cmd_type m)
    {
        switch (m)
        {
        case EVHTTP_REQ_GET:    return "GET";
        case EVHTTP_REQ_POST:   return "POST";
        case EVHTTP_REQ_PUT:    return "PUT";
        case EVHTTP_REQ_DELETE: return "DELETE";
        case EVHTTP_REQ_PATCH:  return "PATCH";
        default:                return "?";
        }
    }

    // \u4ece\u8def\u5f84\u63a8\u65ad target_type / target_id\uff08\u4e0d\u80fd\u63a8\u65ad\u65f6\u8fd4\u56de\u7a7a\u4e32\uff09
    void infer_target(const std::string &path, std::string &target_type, std::string &target_id)
    {
        target_type.clear(); target_id.clear();
        if (path.size() < 6 || path.compare(0, 5, "/api/") != 0) return;
        std::vector<std::string> seg;
        size_t pos = 5;
        while (pos < path.size())
        {
            size_t slash = path.find('/', pos);
            std::string s = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            if (!s.empty()) seg.push_back(s);
            if (slash == std::string::npos) break;
            pos = slash + 1;
        }
        if (seg.empty()) return;
        target_type = seg.front();
        if (seg.size() >= 2) target_id = seg.back();
    }
}

void http_handler::write_audit(const HttpRequest &req, const HttpResponse &resp,
                               const std::string &actor, const std::string &client_ip)
{
    // Only log mutating requests
    if (req.method != EVHTTP_REQ_POST &&
        req.method != EVHTTP_REQ_PUT &&
        req.method != EVHTTP_REQ_DELETE &&
        req.method != EVHTTP_REQ_PATCH)
        return;
    // Skip noisy auth endpoints (login response leaks token), but record logout.
    if (req.path == "/api/auth/login") return;

    auto &redis = sync_redis::instance();
    long long id = redis.incr(KEY_AUDIT_SEQ);

    json payload = nullptr;
    if (!req.body.empty())
    {
        try { payload = json::parse(req.body); mask_secrets(payload); }
        catch (...) { payload = req.body; }
    }

    std::string target_type, target_id;
    infer_target(req.path, target_type, target_id);

    json entry = {
        {"id",          id},
        {"timestamp",   std::time(nullptr)},
        {"actor",       actor.empty() ? std::string{"anonymous"} : actor},
        {"source_ip",   client_ip},
        {"node_id",     CASTER::Get_Node_ID()},
        {"action",      method_str(req.method) + " " + req.path},
        {"target_type", target_type},
        {"target_id",   target_id},
        {"payload",     payload.is_null() ? "" : payload.dump()},
        {"result",      resp.status_code}
    };
    if (resp.status_code >= 400)
    {
        try
        {
            auto err = json::parse(resp.body);
            if (err.is_object() && err.contains("error"))
                entry["error_message"] = err["error"].get<std::string>();
        }
        catch (...) {}
    }
    std::string s = entry.dump();
    redis.lpush(KEY_AUDIT_LOG, s);
    redis.ltrim(KEY_AUDIT_LOG, 0, AUDIT_KEEP - 1);
}

void http_handler::handle_get_audit(const HttpRequest &req, HttpResponse &resp)
{
    long long limit = 100;
    long long cursor = 0;
    std::string filter_actor, filter_action, filter_target;
    auto it_l = req.query_params.find("limit");
    if (it_l != req.query_params.end()) try { limit = std::stoll(it_l->second); } catch (...) {}
    if (limit <= 0 || limit > 1000) limit = 100;
    auto it_c = req.query_params.find("cursor");
    if (it_c != req.query_params.end()) try { cursor = std::stoll(it_c->second); } catch (...) {}
    auto it_a = req.query_params.find("actor");  if (it_a != req.query_params.end()) filter_actor = it_a->second;
    auto it_x = req.query_params.find("action"); if (it_x != req.query_params.end()) filter_action = it_x->second;
    auto it_t = req.query_params.find("target"); if (it_t != req.query_params.end()) filter_target = it_t->second;

    auto &redis = sync_redis::instance();
    long long start = cursor;
    long long stop = cursor + limit * 4 - 1; // \u591a\u62c9\u4e00\u4e9b\u4f9b\u8fc7\u6ee4
    json arr = redis.lrange(KEY_AUDIT_LOG, start, stop);

    json out = json::array();
    long long scanned = 0;
    for (auto &entry : arr)
    {
        scanned++;
        if (!entry.is_object()) continue;
        if (!filter_actor.empty() && entry.value("actor", "") != filter_actor) continue;
        if (!filter_action.empty() && entry.value("action", "").find(filter_action) == std::string::npos) continue;
        if (!filter_target.empty() && entry.value("target_type", "") != filter_target) continue;
        out.push_back(entry);
        if ((long long)out.size() >= limit) break;
    }
    long long next_cursor = start + scanned;
    long long total = redis.llen(KEY_AUDIT_LOG);

    json result = {
        {"items",       out},
        {"next_cursor", next_cursor},
        {"has_more",    next_cursor < total},
        {"total",       total}
    };
    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_get_logs_ring(const HttpRequest &req, HttpResponse &resp)
{
    size_t n = 500;
    auto it_n = req.query_params.find("n");
    if (it_n != req.query_params.end()) try { n = std::stoul(it_n->second); } catch (...) {}
    if (n == 0 || n > 5000) n = 500;

    std::string level_str;
    auto it_l = req.query_params.find("level");
    if (it_l != req.query_params.end()) level_str = it_l->second;
    int min_level = 0;
    if (!level_str.empty())
    {
        spdlog::level::level_enum lvl = spdlog::level::from_str(level_str);
        min_level = static_cast<int>(lvl);
    }

    auto raw = ring_log_view::last_n(n);
    json items = json::array();
    for (auto &line : raw)
    {
        // \u7b80\u6613\u63a8\u65ad\u7ea7\u522b\uff1a\u67e5\u627e [info]/[warn]/[err]/[critical] \u5173\u952e\u5b57
        int lvl = 2; // info
        if (line.find("[debug]")     != std::string::npos) lvl = 1;
        else if (line.find("[trace]") != std::string::npos) lvl = 0;
        else if (line.find("[warning]") != std::string::npos || line.find("[warn]") != std::string::npos) lvl = 3;
        else if (line.find("[error]") != std::string::npos || line.find("[err]")  != std::string::npos) lvl = 4;
        else if (line.find("[critical]") != std::string::npos) lvl = 5;
        if (lvl < min_level) continue;
        json e = {
            {"timestamp", static_cast<long long>(std::time(nullptr)) * 1000LL},
            {"level",     lvl},
            {"category",  "log"},
            {"message",   line}
        };
        items.push_back(e);
    }
    json result = {{"items", items}, {"count", items.size()}};
    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_get_system_events(const HttpRequest &req, HttpResponse &resp)
{
    long long limit = 100;
    auto it_l = req.query_params.find("limit");
    if (it_l != req.query_params.end()) try { limit = std::stoll(it_l->second); } catch (...) {}
    if (limit <= 0 || limit > 500) limit = 100;

    auto &redis = sync_redis::instance();
    auto keys = redis.scan_all_keys(500);
    std::vector<std::string> node_keys;
    for (auto &k : keys)
        if (k.size() > 9 && k.compare(0, 9, "LOG:NODE:") == 0)
            node_keys.push_back(k);

    json items = json::array();
    for (auto &k : node_keys)
    {
        json arr = redis.lrange(k.c_str(), 0, limit - 1);
        for (auto &e : arr)
            if (e.is_object()) items.push_back(e);
    }
    // \u6309 timestamp \u964d\u5e8f
    std::sort(items.begin(), items.end(), [](const json &a, const json &b){
        return a.value("timestamp", 0ULL) > b.value("timestamp", 0ULL);
    });
    if ((long long)items.size() > limit)
        items.erase(items.begin() + limit, items.end());

    json result = {{"items", items}, {"count", items.size()}};
    resp.status_code = 200;
    resp.body = result.dump();
}

void http_handler::handle_set_node_log_level(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    if (id.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing node id"})"; return; }
    json body;
    try { body = json::parse(req.body); }
    catch (...) { resp.status_code = 400; resp.body = R"({"error":"Invalid JSON"})"; return; }
    std::string level = body.value("level", "");
    if (level.empty()) { resp.status_code = 400; resp.body = R"({"error":"Missing level"})"; return; }

    std::string my_id = CASTER::Get_Node_ID();
    if (id != my_id && id != "self" && id != "current")
    {
        // \u8de8\u8282\u70b9\u4e0b\u53d1\u9700\u8981 V4 \u63a7\u5236\u901a\u9053\u3002
        resp.status_code = 501;
        resp.body = R"({"error":"Cross-node log level change not implemented"})";
        return;
    }

    spdlog::level::level_enum lvl = spdlog::level::from_str(level);
    if (lvl == spdlog::level::off && level != "off")
    {
        resp.status_code = 400;
        resp.body = R"({"error":"Unknown level"})";
        return;
    }
    spdlog::set_level(lvl);
    json result = {{"node_id", my_id}, {"level", level}, {"ok", true}};
    resp.status_code = 200;
    resp.body = result.dump();
    spdlog::warn("[http]: log level changed to {} (by remote)", level);
}

void http_handler::on_redis_sample_timer(evutil_socket_t /*fd*/, short /*what*/, void *arg)
{
    static_cast<http_handler *>(arg)->sample_redis_history();
}

void http_handler::sample_redis_history()
{
    auto &redis = sync_redis::instance();
    std::string info = redis.info("ALL");
    if (info.empty()) return;

    auto parsed = parse_redis_info(info);
    if (!parsed.contains("memory") || !parsed.contains("stats") || !parsed.contains("clients"))
    {
        spdlog::warn("[{}:{}]: skip redis history sample, INFO missing required sections", __class__, __func__);
        return;
    }

    const auto &memory = parsed["memory"];
    const auto &stats = parsed["stats"];
    const auto &clients = parsed["clients"];
    const auto used_memory = memory.value("used_memory", 0ULL);
    if (used_memory == 0)
    {
        spdlog::warn("[{}:{}]: skip redis history sample, used_memory is 0", __class__, __func__);
        return;
    }

    const auto hits = stats.value("keyspace_hits", 0ULL);
    const auto misses = stats.value("keyspace_misses", 0ULL);
    const double hit_rate = hits + misses > 0 ? static_cast<double>(hits) / static_cast<double>(hits + misses) : 0.0;

    json point = {
        {"t",                         std::time(nullptr)},
        {"used_memory",               used_memory},
        {"used_memory_rss",           memory.value("used_memory_rss", 0ULL)},
        {"used_memory_peak",          memory.value("used_memory_peak", 0ULL)},
        {"mem_fragmentation_ratio",   memory.value("mem_fragmentation_ratio", 0.0)},
        {"total_keys",                redis.dbsize()},
        {"ops_per_sec",               stats.value("instantaneous_ops_per_sec", 0.0)},
        {"total_commands_processed",  stats.value("total_commands_processed", 0ULL)},
        {"total_connections_received", stats.value("total_connections_received", 0ULL)},
        {"hits",                      hits},
        {"misses",                    misses},
        {"hit_rate",                  hit_rate},
        {"connected_clients",         clients.value("connected_clients", 0ULL)},
        {"blocked_clients",           clients.value("blocked_clients", 0ULL)},
        {"input_kbps",                stats.value("instantaneous_input_kbps", 0.0)},
        {"output_kbps",               stats.value("instantaneous_output_kbps", 0.0)}
    };
    redis.lpush(KEY_REDIS_HISTORY, point.dump());
    redis.ltrim(KEY_REDIS_HISTORY, 0, REDIS_HISTORY_KEEP - 1);
}

void http_handler::handle_get_monitor_redis_history(const HttpRequest &req, HttpResponse &resp)
{
    long long minutes = 60; // 1h \u9ed8\u8ba4
    auto it = req.query_params.find("range");
    if (it != req.query_params.end())
    {
        const std::string &r = it->second;
        if (r == "6h")  minutes = 360;
        else if (r == "24h") minutes = 1440;
        else if (r == "7d") minutes = 10080;
        else if (r == "1h")  minutes = 60;
    }
    auto &redis = sync_redis::instance();
    json arr = redis.lrange(KEY_REDIS_HISTORY, 0, minutes - 1);
    // Redis \u5b58\u4ee5 LPUSH\uff08\u6700\u65b0\u5728\u5934\uff09\uff0c\u53cd\u8f6c\u4e3a\u65f6\u95f4\u5347\u5e8f
    json items = json::array();
    for (auto it2 = arr.rbegin(); it2 != arr.rend(); ++it2)
    {
        if (!it2->is_object() || it2->value("t", 0LL) <= 0 || it2->value("used_memory", 0ULL) == 0)
        {
            continue;
        }
        items.push_back(*it2);
    }
    json result = {{"items", items}, {"count", items.size()}};
    resp.status_code = 200;
    resp.body = result.dump();
}
