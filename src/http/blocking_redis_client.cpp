#include "blocking_redis_client.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/time.h>
#endif
#include <hiredis.h>
#include <spdlog/spdlog.h>

#include <cstdlib>

namespace navcaster::storage
{
namespace
{
using json = nlohmann::json;

json parse_redis_value(const char *text)
{
    if (!text)
    {
        return "";
    }
    try
    {
        return json::parse(text);
    }
    catch (...)
    {
        return std::string(text);
    }
}
} // namespace

BlockingRedisClient::~BlockingRedisClient()
{
    if (_ctx)
    {
        redisFree(_ctx);
        _ctx = nullptr;
    }
}

int BlockingRedisClient::init(const std::string &host, int port, const std::string &password)
{
    _host = host;
    _port = port;
    _password = password;
    return reconnect();
}

int BlockingRedisClient::reconnect()
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
        {
            freeReplyObject(reply);
        }
    }
    return 0;
}

json BlockingRedisClient::hgetall(const char *key)
{
    if (!ensure_connected())
    {
        return json::object();
    }

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
            result[field] = parse_redis_value(reply->element[i + 1]->str);
        }
    }
    freeReplyObject(reply);
    return result;
}

json BlockingRedisClient::hget(const char *key, const char *field)
{
    if (!ensure_connected())
    {
        return nullptr;
    }

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
        result = parse_redis_value(reply->str);
    }
    freeReplyObject(reply);
    return result;
}

bool BlockingRedisClient::hset(const char *key, const char *field, const std::string &value)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "HSET %s %s %s", key, field, value.c_str()));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

bool BlockingRedisClient::hsetnx(const char *key, const char *field, const std::string &value)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "HSETNX %s %s %s", key, field, value.c_str()));
    const bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

bool BlockingRedisClient::hdel(const char *key, const char *field)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "HDEL %s %s", key, field));
    const bool ok = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

long long BlockingRedisClient::hlen(const char *key)
{
    if (!ensure_connected())
    {
        return 0;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "HLEN %s", key));
    long long count = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        count = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return count;
}

bool BlockingRedisClient::set(const char *key, const std::string &value)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "SET %s %s", key, value.c_str()));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

bool BlockingRedisClient::setex(const char *key, int seconds, const std::string &value)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "SETEX %s %d %s", key, seconds, value.c_str()));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

json BlockingRedisClient::get(const char *key)
{
    if (!ensure_connected())
    {
        return nullptr;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "GET %s", key));
    if (!reply)
    {
        reconnect();
        return nullptr;
    }

    json result = nullptr;
    if (reply->type == REDIS_REPLY_STRING && reply->str)
    {
        result = parse_redis_value(reply->str);
    }
    freeReplyObject(reply);
    return result;
}

bool BlockingRedisClient::publish(const char *channel, const std::string &message)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "PUBLISH %s %s", channel, message.c_str()));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

json BlockingRedisClient::lrange(const char *key, long long start, long long stop)
{
    if (!ensure_connected())
    {
        return json::array();
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "LRANGE %s %lld %lld", key, start, stop));
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
            result.push_back(parse_redis_value(reply->element[i]->str));
        }
    }
    freeReplyObject(reply);
    return result;
}

std::string BlockingRedisClient::info(const char *section)
{
    if (!ensure_connected())
    {
        return "";
    }

    redisReply *reply = section
        ? static_cast<redisReply *>(redisCommand(_ctx, "INFO %s", section))
        : static_cast<redisReply *>(redisCommand(_ctx, "INFO"));
    if (!reply)
    {
        reconnect();
        return "";
    }

    std::string result;
    if (reply->type == REDIS_REPLY_STRING && reply->str)
    {
        result = reply->str;
    }
    freeReplyObject(reply);
    return result;
}

long long BlockingRedisClient::dbsize()
{
    if (!ensure_connected())
    {
        return 0;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "DBSIZE"));
    long long count = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        count = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return count;
}

std::vector<std::string> BlockingRedisClient::scan_all_keys(int batch)
{
    if (!ensure_connected())
    {
        return {};
    }

    std::vector<std::string> keys;
    unsigned long long cursor = 0;
    do
    {
        auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "SCAN %llu COUNT %d", cursor, batch));
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
                {
                    keys.emplace_back(arr->element[i]->str);
                }
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

json BlockingRedisClient::scan_hgetall_prefix(const char *prefix)
{
    if (!ensure_connected())
    {
        return json::object();
    }

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
                {
                    continue;
                }
                const char *hkey = arr->element[i]->str;
                auto *hreply = static_cast<redisReply *>(redisCommand(_ctx, "HGETALL %s", hkey));
                if (hreply && hreply->type == REDIS_REPLY_ARRAY)
                {
                    for (size_t j = 0; j + 1 < hreply->elements; j += 2)
                    {
                        std::string field = hreply->element[j]->str ? hreply->element[j]->str : "";
                        result[field] = parse_redis_value(hreply->element[j + 1]->str);
                    }
                }
                if (hreply)
                {
                    freeReplyObject(hreply);
                }
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

std::string BlockingRedisClient::type(const char *key)
{
    if (!ensure_connected())
    {
        return "none";
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "TYPE %s", key));
    std::string result = "none";
    if (reply && reply->type == REDIS_REPLY_STATUS && reply->str)
    {
        result = reply->str;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return result;
}

long long BlockingRedisClient::llen(const char *key)
{
    if (!ensure_connected())
    {
        return 0;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "LLEN %s", key));
    long long count = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        count = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return count;
}

long long BlockingRedisClient::memory_usage(const char *key)
{
    if (!ensure_connected())
    {
        return 0;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "MEMORY USAGE %s", key));
    long long bytes = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        bytes = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return bytes;
}

long long BlockingRedisClient::ttl(const char *key)
{
    if (!ensure_connected())
    {
        return -2;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "TTL %s", key));
    long long value = -2;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        value = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return value;
}

long long BlockingRedisClient::incr(const char *key)
{
    if (!ensure_connected())
    {
        return 0;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "INCR %s", key));
    long long value = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        value = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return value;
}

long long BlockingRedisClient::lpush(const char *key, const std::string &value)
{
    if (!ensure_connected())
    {
        return 0;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "LPUSH %s %s", key, value.c_str()));
    long long len = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER)
    {
        len = reply->integer;
    }
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return len;
}

bool BlockingRedisClient::ltrim(const char *key, long long start, long long stop)
{
    if (!ensure_connected())
    {
        return false;
    }

    auto *reply = static_cast<redisReply *>(redisCommand(_ctx, "LTRIM %s %lld %lld", key, start, stop));
    const bool ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
    {
        freeReplyObject(reply);
    }
    else
    {
        reconnect();
    }
    return ok;
}

bool BlockingRedisClient::ensure_connected()
{
    if (_ctx && !_ctx->err)
    {
        return true;
    }
    return reconnect() == 0;
}

} // namespace navcaster::storage
