#pragma once

#include "account_schema.h"
#include "redis_hash_client.h"
#include "repository_status.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

struct AccountRepositoryResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    std::string account;
    std::string error;
    nlohmann::json record;
};

class AccountRepository
{
public:
    explicit AccountRepository(RedisHashClient &redis);

    nlohmann::json list_accounts();
    nlohmann::json get_account(const std::string &account);
    nlohmann::json list_legacy_active_sessions();

    AccountRepositoryResult create_account(nlohmann::json record, std::int64_t now);
    AccountRepositoryResult update_account(const std::string &account, nlohmann::json record, std::int64_t now);
    AccountRepositoryResult delete_account(const std::string &account);

private:
    bool sync_login_index(const account_schema::AccountSyncPlan &plan);

    RedisHashClient &_redis;
};

} // namespace navcaster::storage
