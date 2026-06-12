#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

class RedisHashClient
{
public:
    virtual ~RedisHashClient() = default;

    virtual nlohmann::json hgetall(const char *key) = 0;
    virtual nlohmann::json hget(const char *key, const char *field) = 0;
    virtual bool hset(const char *key, const char *field, const std::string &value) = 0;
    virtual bool hsetnx(const char *key, const char *field, const std::string &value) = 0;
    virtual bool hdel(const char *key, const char *field) = 0;
    virtual nlohmann::json get(const char *key) { return nullptr; }
    virtual bool set(const char *key, const std::string &value) { return false; }
    virtual bool publish(const char *channel, const std::string &message) { return false; }
    virtual nlohmann::json scan_hgetall_prefix(const char *prefix) { return nlohmann::json::object(); }
    virtual nlohmann::json lrange(const char *key, long long start, long long stop) { return nlohmann::json::array(); }
    virtual std::vector<std::string> scan_all_keys(int batch = 1000) { return {}; }
    virtual long long llen(const char *key) { return 0; }
    virtual long long incr(const char *key) { return 0; }
    virtual long long lpush(const char *key, const std::string &value) { return 0; }
    virtual bool ltrim(const char *key, long long start, long long stop) { return false; }
};

} // namespace navcaster::storage
