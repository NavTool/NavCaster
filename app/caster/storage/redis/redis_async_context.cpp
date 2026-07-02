#include "storage/redis/redis_async_context.h"

#include <utility>

#include <async.h>
#include <adapters/libevent.h>

#include "infra/logger.h"

namespace navcaster::caster {

RedisAsyncContext::RedisAsyncContext(std::string role, std::string host, int port)
    : role_(std::move(role)), host_(std::move(host)), port_(port)
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
        role_ = std::move(other.role_);
        host_ = std::move(other.host_);
        port_ = other.port_;
        context_ = other.context_;
        connected_ = other.connected_;
        other.context_ = nullptr;
        other.connected_ = false;
    }
    return *this;
}

bool RedisAsyncContext::connect(event_base *base)
{
    if (!base || connected_) {
        return connected_;
    }

    context_ = redisAsyncConnect(host_.c_str(), port_);
    if (!context_) {
        log_warn("redis context allocation failed for role=" + role_);
        return false;
    }

    if (context_->err) {
        const std::string error = context_->errstr ? context_->errstr : "unknown";
        log_warn("redis async connect failed for role=" + role_ + ": " + error);
        redisAsyncFree(context_);
        context_ = nullptr;
        return false;
    }

    if (redisLibeventAttach(context_, base) != REDIS_OK) {
        log_warn("redis libevent attach failed for role=" + role_);
        redisAsyncFree(context_);
        context_ = nullptr;
        return false;
    }

    connected_ = true;
    return true;
}

void RedisAsyncContext::disconnect()
{
    if (context_) {
        redisAsyncDisconnect(context_);
        context_ = nullptr;
    }
    connected_ = false;
}

} // namespace navcaster::caster
