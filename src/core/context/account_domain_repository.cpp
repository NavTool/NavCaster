#include "account_domain_repository.h"

#include "account_domain.h"
#include "json_record.h"
#include "redis_keys.h"

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

} // namespace navcaster::storage
