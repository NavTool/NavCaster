#pragma once

#include "redis_hash_client.h"

#include <string>
#include <vector>

struct redisContext;

namespace navcaster::storage
{

class BlockingRedisClient : public RedisHashClient
{
public:
    BlockingRedisClient() = default;
    ~BlockingRedisClient() override;

    BlockingRedisClient(const BlockingRedisClient &) = delete;
    BlockingRedisClient &operator=(const BlockingRedisClient &) = delete;

    int init(const std::string &host, int port, const std::string &password);
    int reconnect();

    nlohmann::json hgetall(const char *key) override;
    nlohmann::json hget(const char *key, const char *field) override;
    bool hset(const char *key, const char *field, const std::string &value) override;
    bool hsetnx(const char *key, const char *field, const std::string &value) override;
    bool hdel(const char *key, const char *field) override;
    nlohmann::json get(const char *key) override;
    bool set(const char *key, const std::string &value) override;
    bool setex(const char *key, int seconds, const std::string &value) override;
    bool publish(const char *channel, const std::string &message) override;
    std::string info(const char *section = nullptr) override;
    long long dbsize() override;
    nlohmann::json scan_hgetall_prefix(const char *prefix) override;
    nlohmann::json lrange(const char *key, long long start, long long stop) override;
    std::vector<std::string> scan_all_keys(int batch = 1000) override;
    std::string type(const char *key) override;
    long long hlen(const char *key) override;
    long long llen(const char *key) override;
    long long memory_usage(const char *key) override;
    long long incr(const char *key) override;
    long long lpush(const char *key, const std::string &value) override;
    bool ltrim(const char *key, long long start, long long stop) override;

    long long ttl(const char *key);

private:
    bool ensure_connected();

    redisContext *_ctx = nullptr;
    std::string _host;
    int _port = 6379;
    std::string _password;
};

} // namespace navcaster::storage
