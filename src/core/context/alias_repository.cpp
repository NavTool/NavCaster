#include "alias_repository.h"

#include "json_record.h"
#include "redis_keys.h"

#include <utility>

namespace navcaster::storage
{
namespace
{
std::string alias_uid_from_rule(const nlohmann::json &rule)
{
    std::string uid = rule.value("uid", std::string{});
    if (uid.empty())
    {
        uid = rule.value("alias_name", std::string{});
    }
    if (uid.empty())
    {
        uid = rule.value("alias_mpt", std::string{});
    }
    if (uid.empty())
    {
        uid = rule.value("name", std::string{});
    }
    return uid;
}

AliasRepositoryResult make_result(RepositoryStatus status, const std::string &uid, const std::string &error)
{
    AliasRepositoryResult result;
    result.status = status;
    result.uid = uid;
    result.error = error;
    return result;
}

bool normalize_alias_rule(nlohmann::json rule, const std::string &forced_uid, std::int64_t now, AliasRepositoryResult &plan, std::string *error)
{
    std::string uid = forced_uid.empty() ? alias_uid_from_rule(rule) : forced_uid;
    if (uid.empty())
    {
        if (error)
        {
            *error = "Missing alias uid";
        }
        return false;
    }

    std::string alias_name = rule.value("alias_name", std::string{});
    if (alias_name.empty())
    {
        alias_name = rule.value("alias_mpt", rule.value("name", uid));
    }
    if (alias_name.empty())
    {
        alias_name = uid;
    }
    const std::string source_name = rule.value("source_name", rule.value("source_mpt", std::string{}));
    if (source_name.empty())
    {
        if (error)
        {
            *error = "source_name is required";
        }
        return false;
    }

    rule["uid"] = uid;
    rule["alias_name"] = alias_name;
    rule["source_name"] = source_name;
    rule["enable"] = rule.value("enable", true);
    rule["visible"] = rule.value("visible", true);
    json_record::touch_timestamps(rule, now);

    plan = {};
    plan.status = RepositoryStatus::Ok;
    plan.uid = uid;
    plan.rule = std::move(rule);
    return true;
}
} // namespace

bool build_alias_create_plan(nlohmann::json rule, std::int64_t now, AliasRepositoryResult &plan, std::string *error)
{
    return normalize_alias_rule(std::move(rule), {}, now, plan, error);
}

bool build_alias_update_plan(const std::string &uid, nlohmann::json rule, std::int64_t now, AliasRepositoryResult &plan, std::string *error)
{
    if (uid.empty())
    {
        if (error)
        {
            *error = "Missing ID";
        }
        return false;
    }
    return normalize_alias_rule(std::move(rule), uid, now, plan, error);
}

AliasRepository::AliasRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json AliasRepository::list_aliases()
{
    return _redis.hgetall(redis_keys::ALIAS_RULE);
}

nlohmann::json AliasRepository::get_alias(const std::string &uid)
{
    if (uid.empty())
    {
        return nullptr;
    }
    return _redis.hget(redis_keys::ALIAS_RULE, uid.c_str());
}

AliasRepositoryResult AliasRepository::create_alias(nlohmann::json rule, std::int64_t now)
{
    AliasRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_alias_create_plan(std::move(rule), now, plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, {}, error.empty() ? "Invalid alias" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, {}, e.what());
    }

    if (!_redis.hsetnx(redis_keys::ALIAS_RULE, plan.uid.c_str(), json_record::dump_record(plan.rule)))
    {
        return make_result(RepositoryStatus::Conflict, plan.uid, "Alias already exists");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, plan.uid, "Failed to publish alias change");
    }
    return plan;
}

AliasRepositoryResult AliasRepository::update_alias(const std::string &uid, nlohmann::json rule, std::int64_t now)
{
    AliasRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_alias_update_plan(uid, std::move(rule), now, plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, uid, error.empty() ? "Invalid alias" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, uid, e.what());
    }

    if (!_redis.hset(redis_keys::ALIAS_RULE, plan.uid.c_str(), json_record::dump_record(plan.rule)))
    {
        return make_result(RepositoryStatus::RedisError, plan.uid, "Redis error");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, plan.uid, "Failed to publish alias change");
    }
    return plan;
}

AliasRepositoryResult AliasRepository::delete_alias(const std::string &uid)
{
    if (uid.empty())
    {
        return make_result(RepositoryStatus::Invalid, {}, "Missing ID");
    }
    if (!_redis.hdel(redis_keys::ALIAS_RULE, uid.c_str()))
    {
        return make_result(RepositoryStatus::NotFound, uid, "Not found");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, uid, "Failed to publish alias change");
    }

    AliasRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.uid = uid;
    return result;
}

bool AliasRepository::publish_changed()
{
    return _redis.publish(redis_keys::CASTER_CONF, "ALIAS");
}

} // namespace navcaster::storage
