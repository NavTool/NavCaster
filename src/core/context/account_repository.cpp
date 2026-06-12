#include "account_repository.h"

#include "account_schema.h"
#include "redis_keys.h"

#include <utility>

namespace navcaster::storage
{
namespace
{
AccountRepositoryResult invalid_result(const std::string &message)
{
    AccountRepositoryResult result;
    result.status = RepositoryStatus::Invalid;
    result.error = message;
    return result;
}

AccountRepositoryResult redis_error_result(const std::string &account, const std::string &message)
{
    AccountRepositoryResult result;
    result.status = RepositoryStatus::RedisError;
    result.account = account;
    result.error = message;
    return result;
}
} // namespace

AccountRepository::AccountRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json AccountRepository::list_accounts()
{
    return _redis.hgetall(redis_keys::ACT_RECORD);
}

nlohmann::json AccountRepository::get_account(const std::string &account)
{
    if (account.empty())
    {
        return nullptr;
    }
    return _redis.hget(redis_keys::ACT_RECORD, account.c_str());
}

nlohmann::json AccountRepository::list_legacy_active_sessions()
{
    return _redis.hgetall(redis_keys::STR_ACTIVE_LEGACY);
}

AccountRepositoryResult AccountRepository::create_account(nlohmann::json record, std::int64_t now)
{
    account_schema::AccountSyncPlan plan;
    std::string error;
    try
    {
        if (!account_schema::build_account_sync_plan(std::move(record), now, plan, &error))
        {
            return invalid_result(error.empty() ? "Invalid account" : error);
        }
    }
    catch (const std::exception &e)
    {
        return invalid_result(e.what());
    }

    if (!_redis.hsetnx(redis_keys::ACT_RECORD, plan.account.c_str(), plan.record.dump()))
    {
        AccountRepositoryResult result;
        result.status = RepositoryStatus::Conflict;
        result.account = plan.account;
        result.error = "Account already exists";
        return result;
    }

    if (!sync_login_index(plan))
    {
        return redis_error_result(plan.account, "Failed to sync account login index");
    }

    AccountRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.account = plan.account;
    result.record = std::move(plan.record);
    return result;
}

AccountRepositoryResult AccountRepository::update_account(const std::string &account, nlohmann::json record, std::int64_t now)
{
    if (account.empty())
    {
        return invalid_result("account is required");
    }

    auto current = _redis.hget(redis_keys::ACT_RECORD, account.c_str());
    if (current.is_null())
    {
        AccountRepositoryResult result;
        result.status = RepositoryStatus::NotFound;
        result.account = account;
        result.error = "Account not found";
        return result;
    }

    const std::string body_account = record.value("account", std::string{});
    if (!body_account.empty() && body_account != account)
    {
        return invalid_result("Account field does not match URL");
    }

    record["account"] = account;
    if (!record.contains("create_time") && current.contains("create_time"))
    {
        record["create_time"] = current["create_time"];
    }
    account_schema::AccountSyncPlan plan;
    std::string error;
    try
    {
        account_schema::preserve_existing_password_material(record, current);
        if (!account_schema::build_account_sync_plan(std::move(record), now, plan, &error))
        {
            return invalid_result(error.empty() ? "Invalid account" : error);
        }
    }
    catch (const std::exception &e)
    {
        return invalid_result(e.what());
    }

    if (!_redis.hset(redis_keys::ACT_RECORD, plan.account.c_str(), plan.record.dump()))
    {
        return redis_error_result(plan.account, "Redis error");
    }

    if (!sync_login_index(plan))
    {
        return redis_error_result(plan.account, "Failed to sync account login index");
    }

    AccountRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.account = plan.account;
    result.record = std::move(plan.record);
    return result;
}

AccountRepositoryResult AccountRepository::delete_account(const std::string &account)
{
    account_schema::AccountDeletePlan plan;
    std::string error;
    if (!account_schema::build_account_delete_plan(account, plan, &error))
    {
        return invalid_result(error.empty() ? "Invalid account" : error);
    }

    if (!_redis.hdel(redis_keys::ACT_RECORD, plan.account.c_str()))
    {
        AccountRepositoryResult result;
        result.status = RepositoryStatus::NotFound;
        result.account = plan.account;
        result.error = "Account not found";
        return result;
    }

    _redis.hdel(redis_keys::ACT_ACTIVE, plan.account.c_str());

    AccountRepositoryResult result;
    result.status = RepositoryStatus::Ok;
    result.account = plan.account;
    return result;
}

bool AccountRepository::sync_login_index(const account_schema::AccountSyncPlan &plan)
{
    if (plan.write_active_index)
    {
        return _redis.hset(redis_keys::ACT_ACTIVE, plan.account.c_str(), plan.active_index.dump());
    }
    if (plan.delete_active_index)
    {
        _redis.hdel(redis_keys::ACT_ACTIVE, plan.account.c_str());
    }
    return true;
}

} // namespace navcaster::storage
