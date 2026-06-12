#pragma once

#include <string>

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
};

} // namespace navcaster::storage
