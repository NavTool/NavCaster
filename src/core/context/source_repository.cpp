#include "source_repository.h"

#include "account_schema.h"
#include "redis_keys.h"

#include <utility>

namespace navcaster::storage
{
namespace
{
constexpr int SOURCE_RECORD_TYPE_REAL = 1;
constexpr int SOURCE_DECODE_TYPE_AUTO = 1;
constexpr int SOURCE_DISP_TYPE_SHOW_WHEN_ONLINE = 3;

std::int64_t number_to_i64(const nlohmann::json &value, std::int64_t fallback = 0)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return value.get<std::int64_t>();
    }
    if (value.is_number_float())
    {
        return static_cast<std::int64_t>(value.get<double>());
    }
    return fallback;
}

SourceRepositoryResult make_result(RepositoryStatus status, const std::string &mountpoint, const std::string &error)
{
    SourceRepositoryResult result;
    result.status = status;
    result.mountpoint = mountpoint;
    result.error = error;
    return result;
}

bool normalize_source_record(nlohmann::json record, const std::string &forced_mountpoint, std::int64_t now, SourceRepositoryResult &plan, std::string *error)
{
    std::string mountpoint = record.value("mountpoint", record.value("uid", std::string{}));
    if (!forced_mountpoint.empty())
    {
        const std::string body_mountpoint = record.value("mountpoint", std::string{});
        if (!body_mountpoint.empty() && body_mountpoint != forced_mountpoint)
        {
            if (error)
            {
                *error = "Mountpoint field does not match URL";
            }
            return false;
        }
        mountpoint = forced_mountpoint;
    }

    if (mountpoint.empty())
    {
        if (error)
        {
            *error = "mountpoint is required";
        }
        return false;
    }

    record["mountpoint"] = mountpoint;
    record["uid"] = mountpoint;
    record["source_group_uid"] = account_schema::normalize_group_uid(record.value("source_group_uid", std::string{}));
    record["record_type"] = record.value("record_type", SOURCE_RECORD_TYPE_REAL);
    record["decode_type"] = record.value("decode_type", SOURCE_DECODE_TYPE_AUTO);
    record["display_type"] = record.value("display_type", SOURCE_DISP_TYPE_SHOW_WHEN_ONLINE);
    if (!record.contains("create_time") || number_to_i64(record["create_time"]) <= 0)
    {
        record["create_time"] = now;
    }
    record["update_time"] = now;

    plan = {};
    plan.status = RepositoryStatus::Ok;
    plan.mountpoint = mountpoint;
    plan.record = std::move(record);
    return true;
}
} // namespace

bool build_source_create_plan(nlohmann::json record, std::int64_t now, SourceRepositoryResult &plan, std::string *error)
{
    return normalize_source_record(std::move(record), {}, now, plan, error);
}

bool build_source_update_plan(const std::string &mountpoint, nlohmann::json record, std::int64_t now, SourceRepositoryResult &plan, std::string *error)
{
    if (mountpoint.empty())
    {
        if (error)
        {
            *error = "mountpoint is required";
        }
        return false;
    }
    return normalize_source_record(std::move(record), mountpoint, now, plan, error);
}

SourceRepository::SourceRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json SourceRepository::list_sources()
{
    return _redis.hgetall(redis_keys::MPT_RECORD);
}

nlohmann::json SourceRepository::get_source(const std::string &mountpoint)
{
    if (mountpoint.empty())
    {
        return nullptr;
    }
    return _redis.hget(redis_keys::MPT_RECORD, mountpoint.c_str());
}

SourceRepositoryResult SourceRepository::create_source(nlohmann::json record, std::int64_t now)
{
    SourceRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_source_create_plan(std::move(record), now, plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, {}, error.empty() ? "Invalid source" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, {}, e.what());
    }

    if (!_redis.hsetnx(redis_keys::MPT_RECORD, plan.mountpoint.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::Conflict, plan.mountpoint, "Source already exists");
    }
    return plan;
}

SourceRepositoryResult SourceRepository::update_source(const std::string &mountpoint, nlohmann::json record, std::int64_t now)
{
    SourceRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_source_update_plan(mountpoint, std::move(record), now, plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, mountpoint, error.empty() ? "Invalid source" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, mountpoint, e.what());
    }

    if (!_redis.hset(redis_keys::MPT_RECORD, plan.mountpoint.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::RedisError, plan.mountpoint, "Redis error");
    }
    return plan;
}

SourceRepositoryResult SourceRepository::delete_source(const std::string &mountpoint)
{
    if (mountpoint.empty())
    {
        return make_result(RepositoryStatus::Invalid, {}, "Missing ID");
    }
    if (!_redis.hdel(redis_keys::MPT_RECORD, mountpoint.c_str()))
    {
        return make_result(RepositoryStatus::NotFound, mountpoint, "Not found");
    }
    SourceRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.mountpoint = mountpoint;
    return result;
}

} // namespace navcaster::storage
