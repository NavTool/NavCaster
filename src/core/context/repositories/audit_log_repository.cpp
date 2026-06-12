#include "audit_log_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

AuditLogRepository::AuditLogRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

long long AuditLogRepository::next_id()
{
    return _redis.incr(redis_keys::LOG_AUDIT_SEQ);
}

void AuditLogRepository::append(const nlohmann::json &entry, int keep)
{
    _redis.lpush(redis_keys::LOG_AUDIT, entry.dump());
    _redis.ltrim(redis_keys::LOG_AUDIT, 0, keep - 1);
}

nlohmann::json AuditLogRepository::read(long long start, long long stop)
{
    return _redis.lrange(redis_keys::LOG_AUDIT, start, stop);
}

long long AuditLogRepository::total()
{
    return _redis.llen(redis_keys::LOG_AUDIT);
}

} // namespace navcaster::storage
