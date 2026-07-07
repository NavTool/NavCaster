#include "storage/redis/redis_async_context.h"

#include <utility>

#include <async.h>
#include <adapters/libevent.h>

#include "infra/logger.h"

namespace navcaster::caster {

RedisAsyncContext::RedisAsyncContext(std::string role, std::string host, int port)
    : _role(std::move(role)), _host(std::move(host)), _port(port)
{
}

RedisAsyncContext::~RedisAsyncContext()
{
    disconnect();
}

RedisAsyncContext::RedisAsyncContext(RedisAsyncContext &&other) noexcept
{
    *this = std::move(other);
}

RedisAsyncContext &RedisAsyncContext::operator=(RedisAsyncContext &&other) noexcept
{
    if (this != &other) {
        disconnect();
        _role = std::move(other._role);
        _host = std::move(other._host);
        _port = other._port;
        _context = other._context;
        _connected = other._connected;
        other._context = nullptr;
        other._connected = false;
    }
    return *this;
}

bool RedisAsyncContext::connect(event_base *base)
{
    if (!base || _connected) {
        return _connected;
    }

    _context = redisAsyncConnect(_host.c_str(), _port);
    if (!_context) {
        log_warn("redis context allocation failed for role=" + _role);
        return false;
    }

    if (_context->err) {
        const std::string error = _context->errstr ? _context->errstr : "unknown";
        log_warn("redis async connect failed for role=" + _role + ": " + error);
        redisAsyncFree(_context);
        _context = nullptr;
        return false;
    }

    if (redisLibeventAttach(_context, base) != REDIS_OK) {
        log_warn("redis libevent attach failed for role=" + _role);
        redisAsyncFree(_context);
        _context = nullptr;
        return false;
    }

    _connected = true;
    return true;
}

void RedisAsyncContext::disconnect()
{
    if (_context) {
        redisAsyncDisconnect(_context);
        _context = nullptr;
    }
    _connected = false;
}

} // namespace navcaster::caster
