#include "account_domain_repository.h"

#include "account_domain.h"
#include "json_record.h"
#include "redis_keys.h"

#include <unordered_set>
#include <utility>

namespace navcaster::storage
{
namespace
{
nlohmann::json deleted_username_marker(const std::string &id, std::int64_t now)
{
    return navcaster::account_domain::username_index_record(id, "deleted", now);
}

bool same_fingerprint_or_empty(const nlohmann::json &existing, const std::string &fingerprint)
{
    if (existing.is_null())
    {
        return true;
    }
    if (existing.is_string())
    {
        return existing.get<std::string>() == fingerprint;
    }
    return existing.value("fingerprint", std::string{}) == fingerprint;
}
} // namespace

AccountDomainRepository::AccountDomainRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

AccountDomainResult AccountDomainRepository::make_result(RepositoryStatus status, std::string id, std::string error) const
{
    AccountDomainResult result;
    result.status = status;
    result.id = std::move(id);
    result.error = std::move(error);
    return result;
}

AccountDomainResult AccountDomainRepository::invalid(const std::string &message) const
{
    return make_result(RepositoryStatus::Invalid, {}, message);
}

AccountDomainResult AccountDomainRepository::redis_error(const std::string &id, const std::string &message) const
{
    return make_result(RepositoryStatus::RedisError, id, message);
}

nlohmann::json AccountDomainRepository::get_hash_record(const char *key, const std::string &field)
{
    if (field.empty())
    {
        return nullptr;
    }
    return _redis.hget(key, field.c_str());
}

bool AccountDomainRepository::hset_json(const char *key, const std::string &field, const nlohmann::json &record)
{
    return _redis.hset(key, field.c_str(), json_record::dump_record(record));
}

bool AccountDomainRepository::hsetnx_json(const char *key, const std::string &field, const nlohmann::json &record)
{
    return _redis.hsetnx(key, field.c_str(), json_record::dump_record(record));
}

bool AccountDomainRepository::account_has_group(const std::string &account_id, const std::string &group_id)
{
    const std::string key = redis_keys::acc_group(account_id);
    const auto grant = _redis.hget(key.c_str(), group_id.c_str());
    return grant.is_object() && account_domain::is_active_status(grant);
}

bool AccountDomainRepository::group_is_active(const std::string &group_id)
{
    const auto group = get_hash_record(redis_keys::MPGRP_RECORD, group_id);
    return group.is_object() && account_domain::is_active_status(group);
}

bool AccountDomainRepository::sync_legacy_access_group(const nlohmann::json &group, std::int64_t now)
{
    const std::string group_id = group.value("group_id", std::string{});
    if (group_id.empty())
    {
        return false;
    }
    const nlohmann::json legacy_group = {
        {"uid", group_id},
        {"group_name", group.value("name", group_id)},
        {"create_time", group.value("create_time", now)},
        {"update_time", now},
        {"nearest_mpt_enable", false},
        {"nearest_mpt_source_name", ""},
        {"allow_visible_inside_group", true},
        {"allow_access_inside_group", true},
        {"allow_nearby_inside_group", true},
        {"allow_visible_outside_group", false},
        {"allow_access_outside_group", false},
        {"allow_nearby_outside_group", false},
    };
    if (!hset_json(redis_keys::ACCESS_GROUP, group_id, legacy_group))
    {
        return false;
    }
    _redis.publish(redis_keys::CASTER_CONF, "ACCESS");
    return true;
}

bool AccountDomainRepository::sync_legacy_access_item(const std::string &group_id, const nlohmann::json &member)
{
    const std::string mountpoint = member.value("mountpoint", std::string{});
    if (group_id.empty() || mountpoint.empty())
    {
        return false;
    }
    const bool active = account_domain::is_active_status(member);
    const int state = active ? 1 : 2;
    const nlohmann::json legacy_item = {
        {"uid", mountpoint},
        {"mount_point_name", mountpoint},
        {"allow_visible", state},
        {"allow_access", state},
        {"allow_nearby", state},
    };
    const std::string key = redis_keys::access_item(group_id);
    if (!hset_json(key.c_str(), mountpoint, legacy_item))
    {
        return false;
    }
    _redis.publish(redis_keys::CASTER_CONF, "ACCESS");
    return true;
}

void AccountDomainRepository::publish_access_status_update(const std::string &username, const std::string &reason)
{
    if (username.empty())
    {
        return;
    }
    const nlohmann::json message = {
        {"type", 1},
        {"connect_key", ""},
        {"channel", username},
        {"Para", ""},
        {"status", -1},
        {"reason", reason.empty() ? "access_account_status_changed" : reason},
    };
    _redis.publish(redis_keys::AUTH_BROADCAST, json_record::dump_record(message));
}

void AccountDomainRepository::refresh_owner_access_indexes(const nlohmann::json &owner, std::int64_t now, const std::string &reason)
{
    const std::string owner_account_id = owner.value("account_id", std::string{});
    if (owner_account_id.empty())
    {
        return;
    }
    const std::string owner_key = redis_keys::aacc_owner(owner_account_id);
    const auto summaries = _redis.hgetall(owner_key.c_str());
    if (!summaries.is_object())
    {
        return;
    }
    for (auto it = summaries.begin(); it != summaries.end(); ++it)
    {
        const std::string access_account_id = it.key();
        const auto access = get_hash_record(redis_keys::AACC_RECORD, access_account_id);
        if (!access.is_object())
        {
            continue;
        }
        const auto summary = account_domain::access_account_owner_summary(access);
        hset_json(owner_key.c_str(), access_account_id, summary);

        const std::string username = access.value("username", std::string{});
        if (account_domain::is_active_status(access) && account_domain::is_active_status(owner))
        {
            hset_json(redis_keys::AACC_ACTIVE, username, account_domain::access_account_auth_index(access, owner));
        }
        else
        {
            _redis.hdel(redis_keys::AACC_ACTIVE, username.c_str());
            publish_access_status_update(username, reason.empty() ? "owner_account_disabled" : reason);
        }
    }
    (void)now;
}

AccountDomainResult AccountDomainRepository::create_account(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_account_record(record, now, &error))
    {
        return invalid(error);
    }

    const std::string account_id = record.value("account_id", std::string{});
    const std::string username = record.value("username", std::string{});
    if (!get_hash_record(redis_keys::ACC_RECORD, account_id).is_null())
    {
        return make_result(RepositoryStatus::Conflict, account_id, "Account already exists");
    }
    if (!_redis.hsetnx(redis_keys::ACC_USERNAME, username.c_str(), json_record::dump_record(account_domain::username_index_record(account_id, record.value("status", std::string{}), now))))
    {
        return make_result(RepositoryStatus::Conflict, account_id, "Account username already exists or is tombstoned");
    }
    if (!hsetnx_json(redis_keys::ACC_RECORD, account_id, record))
    {
        _redis.hdel(redis_keys::ACC_USERNAME, username.c_str());
        return make_result(RepositoryStatus::Conflict, account_id, "Account already exists");
    }

    AccountDomainResult result;
    result.id = account_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::get_account(const std::string &account_id)
{
    const auto record = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (record.is_null())
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found");
    }
    AccountDomainResult result;
    result.id = account_id;
    result.record = record;
    return result;
}

AccountDomainResult AccountDomainRepository::update_account(const std::string &account_id, nlohmann::json record, std::int64_t now)
{
    if (account_id.empty())
    {
        return invalid("account_id is required");
    }
    auto current = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (current.is_null())
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found");
    }
    if (!account_domain::is_active_status(current))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not active");
    }

    if (record.value("account_id", account_id) != account_id)
    {
        return invalid("account_id mismatch");
    }
    record["account_id"] = account_id;
    if (!record.contains("username"))
    {
        record["username"] = current.value("username", std::string{});
    }
    if (record.value("username", std::string{}) != current.value("username", std::string{}))
    {
        return invalid("account username cannot be changed in NC-051");
    }
    if (!record.contains("create_time") && current.contains("create_time"))
    {
        record["create_time"] = current["create_time"];
    }
    account_domain::preserve_existing_password_material(record, current);

    std::string error;
    if (!account_domain::normalize_account_record(record, now, &error))
    {
        return invalid(error);
    }
    if (!hset_json(redis_keys::ACC_RECORD, account_id, record))
    {
        return redis_error(account_id, "Failed to update account");
    }
    hset_json(redis_keys::ACC_USERNAME, record.value("username", std::string{}), account_domain::username_index_record(account_id, record.value("status", std::string{}), now));
    refresh_owner_access_indexes(record, now, account_domain::is_active_status(record) ? "" : "owner_account_disabled");

    AccountDomainResult result;
    result.id = account_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::delete_account(const std::string &account_id, std::int64_t now)
{
    auto current = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (current.is_null())
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found");
    }
    current["status"] = account_domain::STATUS_DELETED;
    current["delete_time"] = now;
    current["update_time"] = now;
    if (!hset_json(redis_keys::ACC_RECORD, account_id, current))
    {
        return redis_error(account_id, "Failed to tombstone account");
    }
    hset_json(redis_keys::ACC_USERNAME, current.value("username", std::string{}), deleted_username_marker(account_id, now));
    refresh_owner_access_indexes(current, now, "owner_account_deleted");

    AccountDomainResult result;
    result.id = account_id;
    result.record = std::move(current);
    return result;
}

AccountDomainResult AccountDomainRepository::create_mount_point_group(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_mount_point_group(record, now, &error))
    {
        return invalid(error);
    }
    const std::string group_id = record.value("group_id", std::string{});
    if (!hsetnx_json(redis_keys::MPGRP_RECORD, group_id, record))
    {
        return make_result(RepositoryStatus::Conflict, group_id, "MountPointGroup already exists");
    }
    if (!sync_legacy_access_group(record, now))
    {
        return redis_error(group_id, "Failed to sync legacy access group");
    }
    AccountDomainResult result;
    result.id = group_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::add_mount_point_group_member(const std::string &group_id, nlohmann::json member, std::int64_t now)
{
    if (!group_is_active(group_id))
    {
        return make_result(RepositoryStatus::NotFound, group_id, "MountPointGroup not found or inactive");
    }
    const std::string mountpoint = member.value("mountpoint", std::string{});
    if (mountpoint.empty())
    {
        return invalid("mountpoint is required");
    }
    member["group_id"] = group_id;
    member["mountpoint"] = mountpoint;
    member["status"] = member.value("status", std::string(account_domain::STATUS_ACTIVE));
    json_record::touch_timestamps(member, now);
    const std::string key = redis_keys::mpgrp_member(group_id);
    if (!hset_json(key.c_str(), mountpoint, member))
    {
        return redis_error(mountpoint, "Failed to write MountPointGroup member");
    }
    if (!sync_legacy_access_item(group_id, member))
    {
        return redis_error(mountpoint, "Failed to sync legacy access item");
    }
    AccountDomainResult result;
    result.id = mountpoint;
    result.record = std::move(member);
    return result;
}

AccountDomainResult AccountDomainRepository::grant_account_group(const std::string &account_id, nlohmann::json grant, std::int64_t now)
{
    if (!account_domain::is_active_status(get_hash_record(redis_keys::ACC_RECORD, account_id)))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    const std::string group_id = grant.value("group_id", std::string{});
    if (!group_is_active(group_id))
    {
        return make_result(RepositoryStatus::NotFound, group_id, "MountPointGroup not found or inactive");
    }
    grant["account_id"] = account_id;
    grant["group_id"] = group_id;
    grant["status"] = grant.value("status", std::string(account_domain::STATUS_ACTIVE));
    json_record::touch_timestamps(grant, now);
    const std::string key = redis_keys::acc_group(account_id);
    if (!hset_json(key.c_str(), group_id, grant))
    {
        return redis_error(group_id, "Failed to write account group grant");
    }
    AccountDomainResult result;
    result.id = group_id;
    result.record = std::move(grant);
    return result;
}

AccountDomainResult AccountDomainRepository::create_mount_point(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_mount_point(record, now, &error))
    {
        return invalid(error);
    }
    const std::string mountpoint = record.value("mountpoint", std::string{});
    if (!hsetnx_json(redis_keys::MOUNT_RECORD, mountpoint, record))
    {
        return make_result(RepositoryStatus::Conflict, mountpoint, "MountPoint already exists");
    }
    AccountDomainResult result;
    result.id = mountpoint;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::create_access_account(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_access_account(record, now, &error))
    {
        return invalid(error);
    }
    if (!json_record::has_nonempty_string_field(record, "password_hash") &&
        !json_record::has_nonempty_string_field(record, "password"))
    {
        return invalid("access account password is required");
    }
    const std::string access_account_id = record.value("access_account_id", std::string{});
    const std::string owner_account_id = record.value("owner_account_id", std::string{});
    const std::string username = record.value("username", std::string{});
    const std::string group_id = record.value("mount_point_group_id", std::string{});
    if (!get_hash_record(redis_keys::AACC_RECORD, access_account_id).is_null())
    {
        return make_result(RepositoryStatus::Conflict, access_account_id, "AccessAccount already exists");
    }

    const auto owner = get_hash_record(redis_keys::ACC_RECORD, owner_account_id);
    if (!owner.is_object() || !account_domain::is_active_status(owner))
    {
        return make_result(RepositoryStatus::NotFound, owner_account_id, "Owner account not found or inactive");
    }
    if (!account_domain::is_access_kind_allowed_for_role(owner.value("role", std::string{}), record.value("kind", std::string{})))
    {
        return make_result(RepositoryStatus::Invalid, access_account_id, "AccessAccount kind is not allowed for owner role");
    }
    if (record.value("concurrency_limit", 0) > owner.value("concurrency_limit", 0))
    {
        return make_result(RepositoryStatus::Invalid, access_account_id, "AccessAccount concurrency limit exceeds owner limit");
    }
    if (!group_is_active(group_id) || !account_has_group(owner_account_id, group_id))
    {
        return make_result(RepositoryStatus::Invalid, group_id, "AccessAccount group is not granted to owner");
    }

    if (!_redis.hsetnx(redis_keys::AACC_USERNAME, username.c_str(), json_record::dump_record(account_domain::username_index_record(access_account_id, record.value("status", std::string{}), now))))
    {
        return make_result(RepositoryStatus::Conflict, access_account_id, "AccessAccount username already exists or is tombstoned");
    }
    if (!hsetnx_json(redis_keys::AACC_RECORD, access_account_id, record))
    {
        _redis.hdel(redis_keys::AACC_USERNAME, username.c_str());
        return make_result(RepositoryStatus::Conflict, access_account_id, "AccessAccount already exists");
    }
    const auto summary = account_domain::access_account_owner_summary(record);
    const std::string owner_key = redis_keys::aacc_owner(owner_account_id);
    hset_json(owner_key.c_str(), access_account_id, summary);
    if (account_domain::is_active_status(record) && account_domain::is_active_status(owner))
    {
        hset_json(redis_keys::AACC_ACTIVE, username, account_domain::access_account_auth_index(record, owner));
    }

    AccountDomainResult result;
    result.id = access_account_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::get_access_account(const std::string &access_account_id)
{
    const auto record = get_hash_record(redis_keys::AACC_RECORD, access_account_id);
    if (record.is_null())
    {
        return make_result(RepositoryStatus::NotFound, access_account_id, "AccessAccount not found");
    }
    AccountDomainResult result;
    result.id = access_account_id;
    result.record = record;
    return result;
}

AccountDomainResult AccountDomainRepository::update_access_account(const std::string &access_account_id, nlohmann::json record, std::int64_t now)
{
    if (access_account_id.empty())
    {
        return invalid("access_account_id is required");
    }
    auto current = get_hash_record(redis_keys::AACC_RECORD, access_account_id);
    if (current.is_null())
    {
        return make_result(RepositoryStatus::NotFound, access_account_id, "AccessAccount not found");
    }
    if (current.value("status", std::string{}) == account_domain::STATUS_DELETED)
    {
        return make_result(RepositoryStatus::NotFound, access_account_id, "AccessAccount not active");
    }
    if (record.value("access_account_id", access_account_id) != access_account_id)
    {
        return invalid("access_account_id mismatch");
    }

    record["access_account_id"] = access_account_id;
    record["owner_account_id"] = current.value("owner_account_id", std::string{});
    record["username"] = current.value("username", std::string{});
    if (!record.contains("kind"))
    {
        record["kind"] = current.value("kind", std::string{});
    }
    if (record.value("kind", std::string{}) != current.value("kind", std::string{}))
    {
        return invalid("access account kind cannot be changed");
    }
    if (!record.contains("mount_point_group_id"))
    {
        record["mount_point_group_id"] = current.value("mount_point_group_id", std::string{});
    }
    if (!record.contains("status"))
    {
        record["status"] = current.value("status", std::string(account_domain::STATUS_ACTIVE));
    }
    if (!record.contains("concurrency_limit"))
    {
        record["concurrency_limit"] = current.value("concurrency_limit", 1);
    }
    if (!record.contains("expire_time"))
    {
        record["expire_time"] = current.value("expire_time", 0);
    }
    if (!record.contains("create_time") && current.contains("create_time"))
    {
        record["create_time"] = current["create_time"];
    }
    account_domain::preserve_existing_password_material(record, current);

    std::string error;
    if (!account_domain::normalize_access_account(record, now, &error))
    {
        return invalid(error);
    }

    const std::string owner_account_id = record.value("owner_account_id", std::string{});
    const std::string group_id = record.value("mount_point_group_id", std::string{});
    const auto owner = get_hash_record(redis_keys::ACC_RECORD, owner_account_id);
    if (!owner.is_object() || !account_domain::is_active_status(owner))
    {
        return make_result(RepositoryStatus::NotFound, owner_account_id, "Owner account not found or inactive");
    }
    if (!account_domain::is_access_kind_allowed_for_role(owner.value("role", std::string{}), record.value("kind", std::string{})))
    {
        return make_result(RepositoryStatus::Invalid, access_account_id, "AccessAccount kind is not allowed for owner role");
    }
    if (record.value("concurrency_limit", 0) > owner.value("concurrency_limit", 0))
    {
        return make_result(RepositoryStatus::Invalid, access_account_id, "AccessAccount concurrency limit exceeds owner limit");
    }
    if (!group_is_active(group_id) || !account_has_group(owner_account_id, group_id))
    {
        return make_result(RepositoryStatus::Invalid, group_id, "AccessAccount group is not granted to owner");
    }

    if (!hset_json(redis_keys::AACC_RECORD, access_account_id, record))
    {
        return redis_error(access_account_id, "Failed to update AccessAccount");
    }
    const auto summary = account_domain::access_account_owner_summary(record);
    const std::string owner_key = redis_keys::aacc_owner(owner_account_id);
    hset_json(owner_key.c_str(), access_account_id, summary);
    const std::string username = record.value("username", std::string{});
    hset_json(redis_keys::AACC_USERNAME, username, account_domain::username_index_record(access_account_id, record.value("status", std::string{}), now));
    if (account_domain::is_active_status(record) && account_domain::is_active_status(owner))
    {
        hset_json(redis_keys::AACC_ACTIVE, username, account_domain::access_account_auth_index(record, owner));
    }
    else
    {
        _redis.hdel(redis_keys::AACC_ACTIVE, username.c_str());
        publish_access_status_update(username, "access_account_disabled");
    }

    AccountDomainResult result;
    result.id = access_account_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::delete_access_account(const std::string &access_account_id, std::int64_t now)
{
    auto current = get_hash_record(redis_keys::AACC_RECORD, access_account_id);
    if (current.is_null())
    {
        return make_result(RepositoryStatus::NotFound, access_account_id, "AccessAccount not found");
    }
    current["status"] = account_domain::STATUS_DELETED;
    current["delete_time"] = now;
    current["update_time"] = now;
    if (!hset_json(redis_keys::AACC_RECORD, access_account_id, current))
    {
        return redis_error(access_account_id, "Failed to tombstone AccessAccount");
    }
    const std::string username = current.value("username", std::string{});
    hset_json(redis_keys::AACC_USERNAME, username, deleted_username_marker(access_account_id, now));
    _redis.hdel(redis_keys::AACC_ACTIVE, username.c_str());
    publish_access_status_update(username, "access_account_deleted");
    const std::string owner_key = redis_keys::aacc_owner(current.value("owner_account_id", std::string{}));
    _redis.hdel(owner_key.c_str(), access_account_id.c_str());

    AccountDomainResult result;
    result.id = access_account_id;
    result.record = std::move(current);
    return result;
}

AccountDomainResult AccountDomainRepository::create_subscription(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_subscription(record, now, &error))
    {
        return invalid(error);
    }
    const std::string subscription_id = record.value("subscription_id", std::string{});
    const std::string account_id = record.value("account_id", std::string{});
    if (!account_domain::is_active_status(get_hash_record(redis_keys::ACC_RECORD, account_id)))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    for (const auto &group_id : record["group_ids"])
    {
        if (!group_id.is_string() || !group_is_active(group_id.get<std::string>()))
        {
            return make_result(RepositoryStatus::Invalid, subscription_id, "Subscription references unknown group");
        }
    }
    if (!hsetnx_json(redis_keys::SUB_RECORD, subscription_id, record))
    {
        return make_result(RepositoryStatus::Conflict, subscription_id, "Subscription already exists");
    }
    const std::string account_key = redis_keys::sub_account(account_id);
    hset_json(account_key.c_str(), subscription_id, record);

    AccountDomainResult result;
    result.id = subscription_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::get_subscription(const std::string &subscription_id)
{
    const auto record = get_hash_record(redis_keys::SUB_RECORD, subscription_id);
    if (record.is_null())
    {
        return make_result(RepositoryStatus::NotFound, subscription_id, "Subscription not found");
    }
    AccountDomainResult result;
    result.id = subscription_id;
    result.record = record;
    return result;
}

AccountDomainResult AccountDomainRepository::update_subscription(const std::string &subscription_id, nlohmann::json record, std::int64_t now)
{
    auto current = get_hash_record(redis_keys::SUB_RECORD, subscription_id);
    if (!current.is_object() || current.value("status", std::string{}) == account_domain::STATUS_DELETED)
    {
        return make_result(RepositoryStatus::NotFound, subscription_id, "Subscription not found");
    }
    record["subscription_id"] = subscription_id;
    if (!record.contains("account_id"))
    {
        record["account_id"] = current.value("account_id", std::string{});
    }
    if (!record.contains("group_ids") && current.contains("group_ids"))
    {
        record["group_ids"] = current["group_ids"];
    }
    if (!record.contains("start_time") && current.contains("start_time"))
    {
        record["start_time"] = current["start_time"];
    }
    if (!record.contains("expire_time") && current.contains("expire_time"))
    {
        record["expire_time"] = current["expire_time"];
    }
    if (!record.contains("status") && current.contains("status"))
    {
        record["status"] = current["status"];
    }
    if (!record.contains("create_time") && current.contains("create_time"))
    {
        record["create_time"] = current["create_time"];
    }

    std::string error;
    if (!account_domain::normalize_subscription(record, now, &error))
    {
        return invalid(error);
    }
    const std::string account_id = record.value("account_id", std::string{});
    if (!account_domain::is_active_status(get_hash_record(redis_keys::ACC_RECORD, account_id)))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    for (const auto &group_id : record["group_ids"])
    {
        if (!group_id.is_string() || !group_is_active(group_id.get<std::string>()))
        {
            return make_result(RepositoryStatus::Invalid, subscription_id, "Subscription references unknown group");
        }
    }

    const std::string previous_account_id = current.value("account_id", std::string{});
    if (!hset_json(redis_keys::SUB_RECORD, subscription_id, record))
    {
        return redis_error(subscription_id, "Failed to update Subscription");
    }
    if (!previous_account_id.empty() && previous_account_id != account_id)
    {
        const std::string previous_key = redis_keys::sub_account(previous_account_id);
        _redis.hdel(previous_key.c_str(), subscription_id.c_str());
    }
    const std::string account_key = redis_keys::sub_account(account_id);
    hset_json(account_key.c_str(), subscription_id, record);

    AccountDomainResult result;
    result.id = subscription_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::delete_subscription(const std::string &subscription_id, std::int64_t now)
{
    auto current = get_hash_record(redis_keys::SUB_RECORD, subscription_id);
    if (!current.is_object() || current.value("status", std::string{}) == account_domain::STATUS_DELETED)
    {
        return make_result(RepositoryStatus::NotFound, subscription_id, "Subscription not found");
    }
    current["status"] = account_domain::STATUS_DELETED;
    current["delete_time"] = now;
    current["update_time"] = now;
    if (!hset_json(redis_keys::SUB_RECORD, subscription_id, current))
    {
        return redis_error(subscription_id, "Failed to tombstone Subscription");
    }
    const std::string account_key = redis_keys::sub_account(current.value("account_id", std::string{}));
    _redis.hdel(account_key.c_str(), subscription_id.c_str());

    AccountDomainResult result;
    result.id = subscription_id;
    result.record = std::move(current);
    return result;
}

AccountDomainResult AccountDomainRepository::create_redeem_code(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_redeem_code(record, now, &error))
    {
        return invalid(error);
    }
    const std::string code = record.value("code", std::string{});
    if (!hsetnx_json(redis_keys::REDEEM_CODE, code, record))
    {
        return make_result(RepositoryStatus::Conflict, code, "RedeemCode already exists");
    }
    AccountDomainResult result;
    result.id = code;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::redeem_code(const std::string &code,
                                                         const std::string &account_id,
                                                         nlohmann::json request,
                                                         const std::string &period,
                                                         std::int64_t now)
{
    if (!request.is_object())
    {
        request = nlohmann::json::object();
    }

    auto code_record = get_hash_record(redis_keys::REDEEM_CODE, code);
    if (!code_record.is_object())
    {
        return make_result(RepositoryStatus::NotFound, code, "RedeemCode not found");
    }
    if (!account_domain::is_active_status(code_record))
    {
        return make_result(RepositoryStatus::Conflict, code, "RedeemCode is not active");
    }
    const std::int64_t expire_time = json_record::as_i64(code_record.value("expire_time", 0), 0);
    if (expire_time > 0 && now > expire_time)
    {
        return make_result(RepositoryStatus::Conflict, code, "RedeemCode expired");
    }
    const std::int64_t amount_cents = json_record::as_i64(code_record.value("amount_cents", 0), 0);
    const std::int64_t redeemed_count = json_record::as_i64(code_record.value("redeemed_count", 0), 0);
    const std::int64_t max_redemptions = json_record::as_i64(code_record.value("max_redemptions", 1), 1);
    if (amount_cents <= 0 || (max_redemptions > 0 && redeemed_count >= max_redemptions))
    {
        return make_result(RepositoryStatus::Conflict, code, "RedeemCode redemption limit reached");
    }

    auto account = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (!account.is_object() || !account_domain::is_active_status(account))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    const std::string redemption_id = request.value("redemption_id", std::string("redeem:") + code + ":" + account_id);
    const std::string ledger_id = request.value("ledger_id", std::string("ledger:redeem:") + code + ":" + account_id);
    const std::int64_t balance_cents = json_record::as_i64(account.value("balance_cents", 0), 0);
    const std::int64_t balance_after_cents = balance_cents + amount_cents;
    const std::string account_key = redis_keys::redeem_account(account_id);
    if (!_redis.hget(account_key.c_str(), redemption_id.c_str()).is_null())
    {
        return make_result(RepositoryStatus::Conflict, redemption_id, "RedeemCode already redeemed by account");
    }
    const auto account_redemptions = _redis.hgetall(account_key.c_str());
    if (account_redemptions.is_object())
    {
        for (const auto &redemption : account_redemptions)
        {
            if (redemption.is_object() && redemption.value("code", std::string{}) == code)
            {
                return make_result(RepositoryStatus::Conflict, code, "RedeemCode already redeemed by account");
            }
        }
    }
    const std::string ledger_key = redis_keys::acc_balance_ledger(period);
    if (!_redis.hget(ledger_key.c_str(), ledger_id.c_str()).is_null())
    {
        return make_result(RepositoryStatus::Conflict, ledger_id, "Balance ledger entry already exists");
    }

    std::string error;
    nlohmann::json redemption = {
        {"redemption_id", redemption_id},
        {"code", code},
        {"account_id", account_id},
        {"amount_cents", amount_cents},
        {"balance_after_cents", balance_after_cents},
        {"ledger_id", ledger_id},
        {"source", "redeem_code"},
    };
    if (request.contains("operator_note"))
    {
        redemption["operator_note"] = request["operator_note"];
    }
    if (code_record.contains("batch_id"))
    {
        redemption["batch_id"] = code_record["batch_id"];
    }
    if (!account_domain::normalize_redeem_redemption(redemption, now, &error))
    {
        return invalid(error);
    }
    nlohmann::json ledger = {
        {"ledger_id", ledger_id},
        {"account_id", account_id},
        {"delta_cents", amount_cents},
        {"balance_after_cents", balance_after_cents},
        {"source", "redeem_code"},
        {"redeem_code", code},
        {"redemption_id", redemption_id},
    };
    if (request.contains("operator_note"))
    {
        ledger["operator_note"] = request["operator_note"];
    }
    if (!account_domain::normalize_balance_ledger_entry(ledger, now, &error))
    {
        return invalid(error);
    }

    if (!hsetnx_json(account_key.c_str(), redemption_id, redemption))
    {
        return make_result(RepositoryStatus::Conflict, redemption_id, "RedeemCode already redeemed by account");
    }
    if (!hsetnx_json(ledger_key.c_str(), ledger_id, ledger))
    {
        _redis.hdel(account_key.c_str(), redemption_id.c_str());
        return make_result(RepositoryStatus::Conflict, ledger_id, "Balance ledger entry already exists");
    }

    auto updated_account = account;
    updated_account["balance_cents"] = balance_after_cents;
    updated_account["update_time"] = now;
    if (!hset_json(redis_keys::ACC_RECORD, account_id, updated_account))
    {
        _redis.hdel(account_key.c_str(), redemption_id.c_str());
        _redis.hdel(ledger_key.c_str(), ledger_id.c_str());
        return redis_error(account_id, "Failed to update account balance");
    }

    auto updated_code = code_record;
    updated_code["redeemed_count"] = redeemed_count + 1;
    updated_code["last_redeemed_account_id"] = account_id;
    updated_code["last_redeemed_time"] = now;
    updated_code["update_time"] = now;
    if (!hset_json(redis_keys::REDEEM_CODE, code, updated_code))
    {
        return redis_error(code, "Failed to update RedeemCode");
    }
    refresh_owner_access_indexes(updated_account, now);

    AccountDomainResult result;
    result.id = redemption_id;
    result.record = std::move(redemption);
    return result;
}

AccountDomainResult AccountDomainRepository::upsert_station_record(nlohmann::json record, std::int64_t now)
{
    const std::string requested_mountpoint = record.value("mountpoint", std::string{});
    const auto current = get_hash_record(redis_keys::STATION_RECORD, requested_mountpoint);
    if (current.is_object())
    {
        if (!record.contains("station_id"))
        {
            record["station_id"] = current.value("station_id", std::string{});
        }
        if (!record.contains("first_seen_time"))
        {
            record["first_seen_time"] = current.value("first_seen_time", now);
        }
        if (!record.contains("total_online_seconds"))
        {
            record["total_online_seconds"] = current.value("total_online_seconds", 0);
        }
    }

    std::string error;
    if (!account_domain::normalize_station_record(record, now, &error))
    {
        return invalid(error);
    }
    const std::string mountpoint = record.value("mountpoint", std::string{});
    if (!hset_json(redis_keys::STATION_RECORD, mountpoint, record))
    {
        return redis_error(mountpoint, "Failed to write StationRecord");
    }
    AccountDomainResult result;
    result.id = mountpoint;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::append_station_event(nlohmann::json event, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_station_event(event, now, &error))
    {
        return invalid(error);
    }
    const std::string event_id = event.value("event_id", std::string{});
    const std::string mountpoint = event.value("mountpoint", std::string{});
    const std::string key = redis_keys::station_event(mountpoint);
    if (_redis.lpush(key.c_str(), json_record::dump_record(event)) <= 0)
    {
        return redis_error(event_id, "Failed to append StationEvent");
    }
    AccountDomainResult result;
    result.id = event_id;
    result.record = std::move(event);
    return result;
}

AccountDomainResult AccountDomainRepository::append_balance_ledger(nlohmann::json entry, const std::string &period, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_balance_ledger_entry(entry, now, &error))
    {
        return invalid(error);
    }
    const std::string ledger_id = entry.value("ledger_id", std::string{});
    const std::string key = redis_keys::acc_balance_ledger(period);
    if (!hsetnx_json(key.c_str(), ledger_id, entry))
    {
        return make_result(RepositoryStatus::Conflict, ledger_id, "Balance ledger entry already exists");
    }
    AccountDomainResult result;
    result.id = ledger_id;
    result.record = std::move(entry);
    return result;
}

AccountDomainResult AccountDomainRepository::apply_balance_adjustment(const std::string &account_id,
                                                                      nlohmann::json entry,
                                                                      const std::string &period,
                                                                      std::int64_t now)
{
    auto account = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (!account.is_object() || !account_domain::is_active_status(account))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    if (!entry.contains("ledger_id"))
    {
        return invalid("ledger_id is required");
    }
    entry["account_id"] = account_id;
    const std::int64_t delta_cents = json_record::as_i64(entry.value("delta_cents", 0), 0);
    const std::int64_t balance_cents = json_record::as_i64(account.value("balance_cents", 0), 0);
    const std::int64_t credit_limit_cents = json_record::as_i64(account.value("credit_limit_cents", 0), 0);
    const std::int64_t balance_after_cents = balance_cents + delta_cents;
    if (balance_after_cents + credit_limit_cents < 0)
    {
        return make_result(RepositoryStatus::Conflict, account_id, "Balance insufficient");
    }
    entry["balance_after_cents"] = balance_after_cents;
    std::string error;
    if (!account_domain::normalize_balance_ledger_entry(entry, now, &error))
    {
        return invalid(error);
    }
    const std::string ledger_id = entry.value("ledger_id", std::string{});
    const std::string key = redis_keys::acc_balance_ledger(period);
    if (!hsetnx_json(key.c_str(), ledger_id, entry))
    {
        return make_result(RepositoryStatus::Conflict, ledger_id, "Balance ledger entry already exists");
    }
    auto updated_account = account;
    updated_account["balance_cents"] = balance_after_cents;
    updated_account["update_time"] = now;
    if (!hset_json(redis_keys::ACC_RECORD, account_id, updated_account))
    {
        _redis.hdel(key.c_str(), ledger_id.c_str());
        return redis_error(account_id, "Failed to update account balance");
    }
    refresh_owner_access_indexes(updated_account, now);

    AccountDomainResult result;
    result.id = ledger_id;
    result.record = std::move(entry);
    return result;
}

AccountDomainResult AccountDomainRepository::append_billing_usage(nlohmann::json entry, const std::string &period, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_billing_usage_entry(entry, now, &error))
    {
        return invalid(error);
    }
    const std::string billing_id = entry.value("billing_id", std::string{});
    const std::string fingerprint = entry.value("fingerprint", std::string{});
    const auto existing_idempotency = _redis.hget(redis_keys::BILL_IDEMPOTENT, billing_id.c_str());
    if (!same_fingerprint_or_empty(existing_idempotency, fingerprint))
    {
        return make_result(RepositoryStatus::Conflict, billing_id, "Billing idempotency fingerprint mismatch");
    }
    if (existing_idempotency.is_null())
    {
        if (!hsetnx_json(redis_keys::BILL_IDEMPOTENT, billing_id, {{"fingerprint", fingerprint}, {"period", period}, {"create_time", now}}))
        {
            return make_result(RepositoryStatus::Conflict, billing_id, "Billing idempotency key already exists");
        }
        const std::string entry_key = redis_keys::bill_entry(period);
        if (!hsetnx_json(entry_key.c_str(), billing_id, entry))
        {
            return make_result(RepositoryStatus::Conflict, billing_id, "Billing entry already exists");
        }
        const std::string account_key = redis_keys::bill_account(entry.value("account_id", std::string{}), period);
        _redis.lpush(account_key.c_str(), billing_id);
    }

    AccountDomainResult result;
    result.id = billing_id;
    result.record = std::move(entry);
    return result;
}

AccountDomainResult AccountDomainRepository::get_data_push_config(const std::string &config_id)
{
    const auto record = get_hash_record(redis_keys::DATA_PUSH_CONFIG, config_id);
    if (!record.is_object() || record.value("status", std::string{}) == account_domain::STATUS_DELETED)
    {
        return make_result(RepositoryStatus::NotFound, config_id, "DataPushConfig not found");
    }
    AccountDomainResult result;
    result.id = config_id;
    result.record = record;
    return result;
}

AccountDomainResult AccountDomainRepository::create_data_push_config(nlohmann::json record, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_data_push_config(record, now, &error))
    {
        return invalid(error);
    }
    const std::string config_id = record.value("config_id", std::string{});
    if (!hsetnx_json(redis_keys::DATA_PUSH_CONFIG, config_id, record))
    {
        return make_result(RepositoryStatus::Conflict, config_id, "DataPushConfig already exists");
    }
    AccountDomainResult result;
    result.id = config_id;
    result.record = std::move(record);
    return result;
}

AccountDomainResult AccountDomainRepository::update_data_push_config(const std::string &config_id, nlohmann::json record, std::int64_t now)
{
    const auto current = get_hash_record(redis_keys::DATA_PUSH_CONFIG, config_id);
    if (!current.is_object() || current.value("status", std::string{}) == account_domain::STATUS_DELETED)
    {
        return make_result(RepositoryStatus::NotFound, config_id, "DataPushConfig not found");
    }
    if (!record.is_object())
    {
        record = nlohmann::json::object();
    }
    auto updated = current;
    updated.update(record);
    updated["config_id"] = config_id;
    updated["create_time"] = current.value("create_time", now);
    std::string error;
    if (!account_domain::normalize_data_push_config(updated, now, &error))
    {
        return invalid(error);
    }
    if (!hset_json(redis_keys::DATA_PUSH_CONFIG, config_id, updated))
    {
        return redis_error(config_id, "Failed to update DataPushConfig");
    }
    AccountDomainResult result;
    result.id = config_id;
    result.record = std::move(updated);
    return result;
}

AccountDomainResult AccountDomainRepository::delete_data_push_config(const std::string &config_id, std::int64_t now)
{
    const auto current = get_hash_record(redis_keys::DATA_PUSH_CONFIG, config_id);
    if (!current.is_object() || current.value("status", std::string{}) == account_domain::STATUS_DELETED)
    {
        return make_result(RepositoryStatus::NotFound, config_id, "DataPushConfig not found");
    }
    auto deleted = current;
    deleted["status"] = account_domain::STATUS_DELETED;
    deleted["delete_time"] = now;
    deleted["update_time"] = now;
    if (!hset_json(redis_keys::DATA_PUSH_CONFIG, config_id, deleted))
    {
        return redis_error(config_id, "Failed to delete DataPushConfig");
    }
    AccountDomainResult result;
    result.id = config_id;
    result.record = std::move(deleted);
    return result;
}

AccountDomainResult AccountDomainRepository::append_data_push_usage(nlohmann::json entry, const std::string &period, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_data_push_usage(entry, now, &error))
    {
        return invalid(error);
    }
    const std::string usage_id = entry.value("usage_id", std::string{});
    const std::string key = redis_keys::data_push(period);
    if (!hsetnx_json(key.c_str(), usage_id, entry))
    {
        return make_result(RepositoryStatus::Conflict, usage_id, "DataPushUsage already exists");
    }
    AccountDomainResult result;
    result.id = usage_id;
    result.record = std::move(entry);
    return result;
}

AccountDomainResult AccountDomainRepository::append_data_push_usage_with_balance(nlohmann::json entry, const std::string &period, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_data_push_usage(entry, now, &error))
    {
        return invalid(error);
    }

    const std::string usage_id = entry.value("usage_id", std::string{});
    const std::string account_id = entry.value("account_id", std::string{});
    const std::int64_t actual_debit_cents = json_record::as_i64(entry.value("actual_debit_cents", 0), 0);
    const auto account = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (!account.is_object() || !account_domain::is_active_status(account))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    const std::int64_t balance_cents = json_record::as_i64(account.value("balance_cents", 0), 0);
    const std::int64_t credit_limit_cents = json_record::as_i64(account.value("credit_limit_cents", 0), 0);
    if (actual_debit_cents < 0)
    {
        return invalid("actual_debit_cents must be non-negative");
    }
    if (actual_debit_cents > balance_cents + credit_limit_cents)
    {
        return make_result(RepositoryStatus::Conflict, account_id, "Balance insufficient");
    }
    const std::string ledger_id = "ledger:data_push:" + usage_id;
    if (actual_debit_cents > 0)
    {
        entry["ledger_id"] = ledger_id;
        entry["balance_after_cents"] = balance_cents - actual_debit_cents;
    }

    const std::string usage_key = redis_keys::data_push(period);
    if (!hsetnx_json(usage_key.c_str(), usage_id, entry))
    {
        return make_result(RepositoryStatus::Conflict, usage_id, "DataPushUsage already exists");
    }

    if (actual_debit_cents > 0)
    {
        const std::int64_t balance_after_cents = balance_cents - actual_debit_cents;
        nlohmann::json ledger = {
            {"ledger_id", ledger_id},
            {"account_id", account_id},
            {"delta_cents", -actual_debit_cents},
            {"balance_after_cents", balance_after_cents},
            {"source", "data_push_usage"},
            {"usage_id", usage_id},
            {"target_mountpoint", entry.value("target_mountpoint", std::string{})},
        };
        if (entry.contains("operator_note"))
        {
            ledger["operator_note"] = entry["operator_note"];
        }
        if (!account_domain::normalize_balance_ledger_entry(ledger, now, &error))
        {
            _redis.hdel(usage_key.c_str(), usage_id.c_str());
            return invalid(error);
        }
        const std::string ledger_key = redis_keys::acc_balance_ledger(period);
        if (!hsetnx_json(ledger_key.c_str(), ledger_id, ledger))
        {
            _redis.hdel(usage_key.c_str(), usage_id.c_str());
            return make_result(RepositoryStatus::Conflict, ledger_id, "Balance ledger entry already exists");
        }

        auto updated_account = account;
        updated_account["balance_cents"] = balance_after_cents;
        updated_account["update_time"] = now;
        if (!hset_json(redis_keys::ACC_RECORD, account_id, updated_account))
        {
            _redis.hdel(usage_key.c_str(), usage_id.c_str());
            _redis.hdel(ledger_key.c_str(), ledger_id.c_str());
            return redis_error(account_id, "Failed to update account balance");
        }
        refresh_owner_access_indexes(updated_account, now);
    }

    AccountDomainResult result;
    result.id = usage_id;
    result.record = std::move(entry);
    return result;
}

AccountDomainResult AccountDomainRepository::create_data_push_job(nlohmann::json request, const std::string &period, std::int64_t now)
{
    if (!request.is_object())
    {
        request = nlohmann::json::object();
    }
    const std::string account_id = request.value("account_id", std::string{});
    const std::string config_id = request.value("config_id", std::string{});
    const std::string resolved_period = request.value("period", period.empty() ? std::string("current") : period);
    const std::int64_t used_seconds = json_record::as_i64(request.value("used_seconds", 0), 0);
    if (account_id.empty())
    {
        return invalid("account_id is required");
    }
    if (config_id.empty())
    {
        return invalid("config_id is required");
    }
    if (used_seconds <= 0)
    {
        return invalid("used_seconds must be positive");
    }

    const auto account = get_hash_record(redis_keys::ACC_RECORD, account_id);
    if (!account.is_object() || !account_domain::is_active_status(account))
    {
        return make_result(RepositoryStatus::NotFound, account_id, "Account not found or inactive");
    }
    const std::string role = account.value("role", std::string{});
    if (role != account_domain::ROLE_USER && role != account_domain::ROLE_ADMIN)
    {
        return make_result(RepositoryStatus::Invalid, account_id, "DataPush job account must be user or admin");
    }

    const auto config = get_hash_record(redis_keys::DATA_PUSH_CONFIG, config_id);
    if (!config.is_object() || !account_domain::is_active_status(config))
    {
        return make_result(RepositoryStatus::NotFound, config_id, "DataPushConfig not found or inactive");
    }

    const std::int64_t fixed_hourly_price_cents = json_record::as_i64(config.value("fixed_hourly_price_cents", 0), 0);
    if (fixed_hourly_price_cents < 0)
    {
        return invalid("fixed_hourly_price_cents must be non-negative");
    }
    const std::int64_t stat_cost_cents = fixed_hourly_price_cents <= 0
        ? 0
        : (fixed_hourly_price_cents * used_seconds + 3599) / 3600;
    const std::int64_t actual_debit_cents = stat_cost_cents;
    const std::int64_t balance_cents = json_record::as_i64(account.value("balance_cents", 0), 0);
    const std::int64_t credit_limit_cents = json_record::as_i64(account.value("credit_limit_cents", 0), 0);
    if (actual_debit_cents > balance_cents + credit_limit_cents)
    {
        return make_result(RepositoryStatus::Conflict, account_id, "Balance insufficient");
    }

    const std::string job_id = request.value("job_id", std::string("job:data_push:") + account_id + ":" + resolved_period + ":" + std::to_string(now));
    const std::string usage_id = "usage:data_push:" + job_id;
    const std::string job_key = redis_keys::data_push_job(resolved_period);
    if (_redis.hget(job_key.c_str(), job_id.c_str()).is_object())
    {
        return make_result(RepositoryStatus::Conflict, job_id, "DataPushJob already exists");
    }

    nlohmann::json usage = {
        {"usage_id", usage_id},
        {"account_id", account_id},
        {"config_id", config_id},
        {"job_id", job_id},
        {"target_mountpoint", config.value("target_mountpoint", std::string{})},
        {"group_id", config.value("group_id", std::string{})},
        {"used_seconds", used_seconds},
        {"stat_cost_cents", stat_cost_cents},
        {"actual_debit_cents", actual_debit_cents},
        {"price_snapshot", {
            {"fixed_hourly_price_cents", fixed_hourly_price_cents},
        }},
    };
    if (request.contains("request_id"))
    {
        usage["request_id"] = request["request_id"];
    }
    if (request.contains("operator_note"))
    {
        usage["operator_note"] = request["operator_note"];
    }

    auto usage_result = append_data_push_usage_with_balance(usage, resolved_period, now);
    if (usage_result.status != RepositoryStatus::Ok)
    {
        return usage_result;
    }

    nlohmann::json job = {
        {"job_id", job_id},
        {"account_id", account_id},
        {"config_id", config_id},
        {"target_mountpoint", config.value("target_mountpoint", std::string{})},
        {"group_id", config.value("group_id", std::string{})},
        {"usage_id", usage_id},
        {"period", resolved_period},
        {"used_seconds", used_seconds},
        {"stat_cost_cents", stat_cost_cents},
        {"actual_debit_cents", actual_debit_cents},
        {"status", "completed"},
        {"config_snapshot", config},
        {"price_snapshot", {
            {"fixed_hourly_price_cents", fixed_hourly_price_cents},
        }},
    };
    if (usage_result.record.contains("ledger_id"))
    {
        job["ledger_id"] = usage_result.record["ledger_id"];
    }
    if (usage_result.record.contains("balance_after_cents"))
    {
        job["balance_after_cents"] = usage_result.record["balance_after_cents"];
    }
    if (request.contains("request_id"))
    {
        job["request_id"] = request["request_id"];
    }
    if (request.contains("operator_note"))
    {
        job["operator_note"] = request["operator_note"];
    }

    std::string error;
    if (!account_domain::normalize_data_push_job(job, now, &error))
    {
        _redis.hdel(redis_keys::data_push(resolved_period).c_str(), usage_id.c_str());
        if (usage_result.record.contains("ledger_id"))
        {
            _redis.hdel(redis_keys::acc_balance_ledger(resolved_period).c_str(), usage_result.record.value("ledger_id", std::string{}).c_str());
        }
        return invalid(error);
    }
    if (!hsetnx_json(job_key.c_str(), job_id, job))
    {
        return make_result(RepositoryStatus::Conflict, job_id, "DataPushJob already exists");
    }

    AccountDomainResult result;
    result.id = job_id;
    result.record = std::move(job);
    return result;
}

AccountDomainResult AccountDomainRepository::append_supplier_supply_usage(nlohmann::json entry, const std::string &period, std::int64_t now)
{
    std::string error;
    if (!account_domain::normalize_supplier_supply_usage(entry, now, &error))
    {
        return invalid(error);
    }
    const std::string usage_id = entry.value("usage_id", std::string{});
    const std::string key = redis_keys::supply_usage(period);
    if (!hsetnx_json(key.c_str(), usage_id, entry))
    {
        return make_result(RepositoryStatus::Conflict, usage_id, "SupplierSupplyUsage already exists");
    }
    const std::string account_key = redis_keys::supply_account(entry.value("supplier_account_id", std::string{}), period);
    _redis.lpush(account_key.c_str(), usage_id);
    AccountDomainResult result;
    result.id = usage_id;
    result.record = std::move(entry);
    return result;
}

AccountDomainResult AccountDomainRepository::create_supplier_settlement(nlohmann::json request, const std::string &period, std::int64_t now)
{
    if (!request.is_object())
    {
        request = nlohmann::json::object();
    }
    const std::string supplier_account_id = request.value("supplier_account_id", std::string{});
    if (supplier_account_id.empty())
    {
        return invalid("supplier_account_id is required");
    }
    const std::string resolved_period = request.value("period", period);
    if (resolved_period.empty())
    {
        return invalid("period is required");
    }

    const auto supplier = get_hash_record(redis_keys::ACC_RECORD, supplier_account_id);
    if (!supplier.is_object() || !account_domain::is_active_status(supplier))
    {
        return make_result(RepositoryStatus::NotFound, supplier_account_id, "Supplier account not found or inactive");
    }
    const std::string supplier_role = supplier.value("role", std::string{});
    if (supplier_role != account_domain::ROLE_SUPPLIER && supplier_role != account_domain::ROLE_ADMIN)
    {
        return make_result(RepositoryStatus::Invalid, supplier_account_id, "Settlement account must be supplier or admin");
    }

    std::unordered_set<std::string> requested_usage_ids;
    if (request.contains("usage_ids"))
    {
        if (!request["usage_ids"].is_array() || request["usage_ids"].empty())
        {
            return invalid("usage_ids must be a non-empty array");
        }
        for (const auto &usage_id : request["usage_ids"])
        {
            if (!usage_id.is_string() || usage_id.get<std::string>().empty())
            {
                return invalid("usage_ids must contain strings");
            }
            requested_usage_ids.insert(usage_id.get<std::string>());
        }
    }

    const std::string usage_key = redis_keys::supply_usage(resolved_period);
    const auto all_usage = _redis.hgetall(usage_key.c_str());
    nlohmann::json selected_usage_ids = nlohmann::json::array();
    nlohmann::json selected_records = nlohmann::json::array();
    std::int64_t total_supply_seconds = 0;
    std::int64_t total_earning_cents = 0;
    if (all_usage.is_object())
    {
        for (auto it = all_usage.begin(); it != all_usage.end(); ++it)
        {
            const auto &entry = it.value();
            if (!entry.is_object() ||
                entry.value("supplier_account_id", std::string{}) != supplier_account_id ||
                entry.value("status", std::string("pending")) == "settled")
            {
                continue;
            }
            const std::string usage_id = entry.value("usage_id", it.key());
            if (!requested_usage_ids.empty() && requested_usage_ids.find(usage_id) == requested_usage_ids.end())
            {
                continue;
            }
            selected_usage_ids.push_back(usage_id);
            selected_records.push_back(entry);
            total_supply_seconds += json_record::as_i64(entry.value("used_seconds", 0), 0);
            total_earning_cents += json_record::as_i64(entry.value("earning_cents", 0), 0);
        }
    }
    if (selected_usage_ids.empty())
    {
        return make_result(RepositoryStatus::Conflict, supplier_account_id, "No pending SupplierSupplyUsage found");
    }

    const std::string settlement_id = request.value("settlement_id", std::string("settlement:") + supplier_account_id + ":" + resolved_period + ":" + std::to_string(now));
    nlohmann::json settlement = {
        {"settlement_id", settlement_id},
        {"supplier_account_id", supplier_account_id},
        {"period", resolved_period},
        {"usage_ids", selected_usage_ids},
        {"usage_count", static_cast<int>(selected_usage_ids.size())},
        {"total_supply_seconds", total_supply_seconds},
        {"total_earning_cents", total_earning_cents},
        {"status", request.value("status", std::string("settled"))},
    };
    if (request.contains("operator_note"))
    {
        settlement["operator_note"] = request["operator_note"];
    }
    if (request.contains("external_ref"))
    {
        settlement["external_ref"] = request["external_ref"];
    }

    std::string error;
    if (!account_domain::normalize_supplier_settlement(settlement, now, &error))
    {
        return invalid(error);
    }

    const std::string settlement_key = redis_keys::supply_earning(supplier_account_id, resolved_period);
    if (!hsetnx_json(settlement_key.c_str(), settlement_id, settlement))
    {
        return make_result(RepositoryStatus::Conflict, settlement_id, "SupplierSettlement already exists");
    }

    for (const auto &selected : selected_records)
    {
        nlohmann::json updated = selected;
        const std::string usage_id = updated.value("usage_id", std::string{});
        updated["status"] = "settled";
        updated["settlement_id"] = settlement_id;
        updated["settlement_time"] = now;
        updated["update_time"] = now;
        if (!hset_json(usage_key.c_str(), usage_id, updated))
        {
            return redis_error(usage_id, "Failed to update SupplierSupplyUsage settlement status");
        }
    }

    AccountDomainResult result;
    result.id = settlement_id;
    result.record = std::move(settlement);
    return result;
}

} // namespace navcaster::storage
