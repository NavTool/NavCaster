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

    bool connected() const { return _connected; }
    const std::string &role() const { return _role; }
    const std::string &host() const { return _host; }
    int port() const { return _port; }
    redisAsyncContext *raw() const { return _context; }

private:
    std::string _role;
    std::string _host = "127.0.0.1";
    int _port = 6379;
    redisAsyncContext *_context = nullptr;
    bool _connected = false;
};

} // namespace navcaster::caster
