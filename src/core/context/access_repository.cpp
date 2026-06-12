#include "access_repository.h"

#include "redis_keys.h"

#include <utility>

namespace navcaster::storage
{
namespace
{
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

AccessRepositoryResult make_result(RepositoryStatus status, const std::string &uid, const std::string &error)
{
    AccessRepositoryResult result;
    result.status = status;
    result.uid = uid;
    result.error = error;
    return result;
}

nlohmann::json builtin_group(const std::string &uid, std::int64_t now, bool nearest_enabled)
{
    return {
        {"uid", uid},
        {"group_name", uid},
        {"create_time", now},
        {"update_time", now},
        {"nearest_mpt_enable", nearest_enabled},
        {"nearest_mpt_source_name", ""},
        {"allow_visible_inside_group", true},
        {"allow_access_inside_group", true},
        {"allow_nearby_inside_group", true},
        {"allow_visible_outside_group", true},
        {"allow_access_outside_group", true},
        {"allow_nearby_outside_group", true}};
}

std::string access_item_mountpoint(const nlohmann::json &item)
{
    std::string mount = item.value("mount_point_name", std::string{});
    if (mount.empty())
    {
        mount = item.value("mountpoint", std::string{});
    }
    if (mount.empty())
    {
        mount = item.value("mount", std::string{});
    }
    if (mount.empty())
    {
        mount = item.value("uid", std::string{});
    }
    return mount;
}
} // namespace

bool build_access_group_plan(nlohmann::json group, std::int64_t now, AccessRepositoryResult &plan, std::string *error)
{
    std::string uid = group.value("uid", group.value("group_uid", std::string{}));
    if (uid.empty())
    {
        if (error)
        {
            *error = "Missing group uid";
        }
        return false;
    }

    group["uid"] = uid;
    group["group_name"] = group.value("group_name", uid);
    if (!group.contains("create_time") || number_to_i64(group["create_time"]) <= 0)
    {
        group["create_time"] = now;
    }
    group["update_time"] = now;
    group["nearest_mpt_enable"] = group.value("nearest_mpt_enable", false);
    group["nearest_mpt_source_name"] = group.value("nearest_mpt_source_name", std::string{});
    group["allow_visible_inside_group"] = group.value("allow_visible_inside_group", true);
    group["allow_access_inside_group"] = group.value("allow_access_inside_group", true);
    group["allow_nearby_inside_group"] = group.value("allow_nearby_inside_group", true);
    group["allow_visible_outside_group"] = group.value("allow_visible_outside_group", true);
    group["allow_access_outside_group"] = group.value("allow_access_outside_group", true);
    group["allow_nearby_outside_group"] = group.value("allow_nearby_outside_group", true);

    plan = {};
    plan.status = RepositoryStatus::Ok;
    plan.uid = uid;
    plan.record = std::move(group);
    return true;
}

bool build_access_item_plan(const std::string &group_uid, nlohmann::json item, AccessRepositoryResult &plan, std::string *error)
{
    if (group_uid.empty())
    {
        if (error)
        {
            *error = "Missing group_uid";
        }
        return false;
    }

    std::string mount = access_item_mountpoint(item);
    if (mount.empty())
    {
        if (error)
        {
            *error = "Missing mountpoint";
        }
        return false;
    }

    item["uid"] = item.value("uid", mount);
    if (item.value("uid", std::string{}).empty())
    {
        item["uid"] = mount;
    }
    item["mount_point_name"] = mount;
    item["allow_visible"] = item.value("allow_visible", 0);
    item["allow_access"] = item.value("allow_access", 0);
    item["allow_nearby"] = item.value("allow_nearby", 0);

    plan = {};
    plan.status = RepositoryStatus::Ok;
    plan.group_uid = group_uid;
    plan.mountpoint = mount;
    plan.record = std::move(item);
    return true;
}

AccessRepository::AccessRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

void AccessRepository::ensure_builtin_groups(std::int64_t now)
{
    const auto default_group = builtin_group("default", now, false);
    _redis.hsetnx(redis_keys::ACCESS_GROUP, "default", default_group.dump());
    const auto system_group = builtin_group("SYSTEM", now, true);
    _redis.hsetnx(redis_keys::ACCESS_GROUP, "SYSTEM", system_group.dump());
}

nlohmann::json AccessRepository::list_groups()
{
    return _redis.hgetall(redis_keys::ACCESS_GROUP);
}

nlohmann::json AccessRepository::get_group(const std::string &uid)
{
    if (uid.empty())
    {
        return nullptr;
    }
    return _redis.hget(redis_keys::ACCESS_GROUP, uid.c_str());
}

AccessRepositoryResult AccessRepository::create_group(nlohmann::json group, std::int64_t now)
{
    AccessRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_access_group_plan(std::move(group), now, plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, {}, error.empty() ? "Invalid group" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, {}, e.what());
    }

    if (!_redis.hsetnx(redis_keys::ACCESS_GROUP, plan.uid.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::Conflict, plan.uid, "Group already exists");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, plan.uid, "Failed to publish access change");
    }
    return plan;
}

AccessRepositoryResult AccessRepository::update_group(const std::string &uid, nlohmann::json group, std::int64_t now)
{
    if (uid.empty())
    {
        return make_result(RepositoryStatus::Invalid, {}, "Missing ID");
    }
    group["uid"] = uid;

    AccessRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_access_group_plan(std::move(group), now, plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, uid, error.empty() ? "Invalid group" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, uid, e.what());
    }

    if (!_redis.hset(redis_keys::ACCESS_GROUP, plan.uid.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::RedisError, plan.uid, "Redis error");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, plan.uid, "Failed to publish access change");
    }
    return plan;
}

AccessRepositoryResult AccessRepository::delete_group(const std::string &uid)
{
    if (uid.empty())
    {
        return make_result(RepositoryStatus::Invalid, {}, "Missing ID");
    }
    if (uid == "default" || uid == "SYSTEM")
    {
        return make_result(RepositoryStatus::Invalid, uid, "Built-in group cannot be deleted");
    }
    if (!_redis.hdel(redis_keys::ACCESS_GROUP, uid.c_str()))
    {
        return make_result(RepositoryStatus::NotFound, uid, "Not found");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, uid, "Failed to publish access change");
    }

    AccessRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.uid = uid;
    return result;
}

nlohmann::json AccessRepository::list_items(const std::string &group_uid)
{
    if (group_uid.empty())
    {
        return nlohmann::json::object();
    }
    const std::string key = redis_keys::access_item(group_uid);
    return _redis.hgetall(key.c_str());
}

AccessRepositoryResult AccessRepository::create_item(const std::string &group_uid, nlohmann::json item)
{
    AccessRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_access_item_plan(group_uid, std::move(item), plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, {}, error.empty() ? "Invalid access item" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, {}, e.what());
    }

    const std::string key = redis_keys::access_item(plan.group_uid);
    if (!_redis.hsetnx(key.c_str(), plan.mountpoint.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::Conflict, plan.mountpoint, "Item already exists");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, plan.mountpoint, "Failed to publish access change");
    }
    return plan;
}

AccessRepositoryResult AccessRepository::update_item(const std::string &group_uid, nlohmann::json item)
{
    AccessRepositoryResult plan;
    std::string error;
    try
    {
        if (!build_access_item_plan(group_uid, std::move(item), plan, &error))
        {
            return make_result(RepositoryStatus::Invalid, {}, error.empty() ? "Invalid access item" : error);
        }
    }
    catch (const std::exception &e)
    {
        return make_result(RepositoryStatus::Invalid, {}, e.what());
    }

    const std::string key = redis_keys::access_item(plan.group_uid);
    if (!_redis.hset(key.c_str(), plan.mountpoint.c_str(), plan.record.dump()))
    {
        return make_result(RepositoryStatus::RedisError, plan.mountpoint, "Redis error");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, plan.mountpoint, "Failed to publish access change");
    }
    return plan;
}

AccessRepositoryResult AccessRepository::delete_item(const std::string &group_uid, const std::string &mountpoint)
{
    if (group_uid.empty() || mountpoint.empty())
    {
        return make_result(RepositoryStatus::Invalid, mountpoint, "Missing group_uid or mountpoint");
    }
    const std::string key = redis_keys::access_item(group_uid);
    if (!_redis.hdel(key.c_str(), mountpoint.c_str()))
    {
        return make_result(RepositoryStatus::NotFound, mountpoint, "Item not found");
    }
    if (!publish_changed())
    {
        return make_result(RepositoryStatus::RedisError, mountpoint, "Failed to publish access change");
    }

    AccessRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.group_uid = group_uid;
    result.mountpoint = mountpoint;
    return result;
}

bool AccessRepository::publish_changed()
{
    return _redis.publish(redis_keys::CASTER_CONF, "ACCESS");
}

} // namespace navcaster::storage
