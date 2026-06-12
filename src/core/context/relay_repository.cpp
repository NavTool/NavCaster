#include "relay_repository.h"

#include "redis_keys.h"

#include <utility>

namespace navcaster::storage
{
namespace
{
RelayRepositoryResult make_result(RepositoryStatus status, RelayKind kind, const std::string &uid, const std::string &error)
{
    RelayRepositoryResult result;
    result.status = status;
    result.kind = kind;
    result.uid = uid;
    result.error = error;
    return result;
}
} // namespace

const char *relay_record_key(RelayKind kind)
{
    return kind == RelayKind::Pull ? redis_keys::PULL_RECORD : redis_keys::PUSH_RECORD;
}

const char *relay_state_key(RelayKind kind)
{
    return kind == RelayKind::Pull ? redis_keys::PULL_STAT : redis_keys::PUSH_STAT;
}

bool build_relay_record_plan(RelayKind kind, nlohmann::json record, RelayRepositoryResult &plan, std::string *error)
{
    std::string uid = record.value("uid", std::string{});
    if (uid.empty())
    {
        if (error)
        {
            *error = "Missing uid";
        }
        return false;
    }
    record["uid"] = uid;
    if (!record.contains("enabled"))
    {
        record["enabled"] = true;
    }

    plan = {};
    plan.status = RepositoryStatus::Ok;
    plan.kind = kind;
    plan.uid = uid;
    plan.record = std::move(record);
    return true;
}

RelayRepository::RelayRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json RelayRepository::list_records(RelayKind kind)
{
    return _redis.hgetall(relay_record_key(kind));
}

nlohmann::json RelayRepository::get_record(RelayKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return nullptr;
    }
    return _redis.hget(relay_record_key(kind), uid.c_str());
}

nlohmann::json RelayRepository::list_states(RelayKind kind)
{
    return _redis.hgetall(relay_state_key(kind));
}

RelayRepositoryResult RelayRepository::create_record(RelayKind kind, nlohmann::json record)
{
    RelayRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_relay_record_plan(kind, std::move(record), plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, kind, {}, error.empty() ? "Invalid relay record" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, kind, {}, e.what());
    }

    if (!_redis.hsetnx(relay_record_key(kind), plan.uid.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::Conflict, kind, plan.uid, kind == RelayKind::Pull ? "Pull record already exists" : "Push record already exists");
    }
    return plan;
}

RelayRepositoryResult RelayRepository::update_record(RelayKind kind, const std::string &uid, nlohmann::json record)
{
    if (uid.empty())
    {
        return make_result(RepositoryStatus::Invalid, kind, {}, "Missing ID");
    }
    record["uid"] = uid;

    RelayRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_relay_record_plan(kind, std::move(record), plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, kind, uid, error.empty() ? "Invalid relay record" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, kind, uid, e.what());
    }

    if (!_redis.hset(relay_record_key(kind), plan.uid.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::RedisError, kind, plan.uid, "Redis error");
    }
    _redis.hdel(relay_state_key(kind), plan.uid.c_str());
    return plan;
}

RelayRepositoryResult RelayRepository::delete_record(RelayKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return make_result(RepositoryStatus::Invalid, kind, {}, "Missing ID");
    }
    if (!_redis.hdel(relay_record_key(kind), uid.c_str()))
    {
        return make_result(RepositoryStatus::NotFound, kind, uid, "Not found");
    }
    _redis.hdel(relay_state_key(kind), uid.c_str());

    RelayRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.kind = kind;
    result.uid = uid;
    return result;
}

RelayRepositoryResult RelayRepository::set_enabled(RelayKind kind, const std::string &uid, bool enabled)
{
    if (uid.empty())
    {
        return make_result(RepositoryStatus::Invalid, kind, {}, "Missing ID");
    }

    auto record = _redis.hget(relay_record_key(kind), uid.c_str());
    if (record.is_null())
    {
        return make_result(RepositoryStatus::NotFound, kind, uid, "Record not found");
    }
    if (record.is_string())
    {
        try
        {
            record = nlohmann::json::parse(record.get<std::string>());
        }
        catch (const std::exception &e)
        {
            return make_result(RepositoryStatus::RedisError, kind, uid, e.what());
        }
    }
    if (!record.is_object())
    {
        return make_result(RepositoryStatus::RedisError, kind, uid, "Relay record is invalid");
    }

    record["uid"] = uid;
    record["enabled"] = enabled;
    if (!_redis.hset(relay_record_key(kind), uid.c_str(), record.dump()))
    {
        return make_result(RepositoryStatus::RedisError, kind, uid, "Failed to update record");
    }

    RelayRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.kind = kind;
    result.uid = uid;
    result.record = std::move(record);
    return result;
}

} // namespace navcaster::storage
