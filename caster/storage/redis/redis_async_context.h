#pragma once

#include <memory>
#include <string>

struct event_base;
struct redisAsyncContext;

namespace navcaster::caster {

class RedisAsyncContext {
public:
    RedisAsyncContext() = default;
    RedisAsyncContext(std::string role, std::string host, int port);
    ~RedisAsyncContext();

    RedisAsyncContext(const RedisAsyncContext &) = delete;
    RedisAsyncContext &operator=(const RedisAsyncContext &) = delete;

    RedisAsyncContext(RedisAsyncContext &&other) noexcept;
    RedisAsyncContext &operator=(RedisAsyncContext &&other) noexcept;

    bool connect(event_base *base);
    void disconnect();

    bool connected() const { return connected_; }
    const std::string &role() const { return role_; }
    const std::string &host() const { return host_; }
    int port() const { return port_; }
    redisAsyncContext *raw() const { return context_; }

private:
    std::string role_;
    std::string host_ = "127.0.0.1";
    int port_ = 6379;
    redisAsyncContext *context_ = nullptr;
    bool connected_ = false;
};

} // namespace navcaster::caster
