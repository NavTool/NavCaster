#pragma once

#include "account_domain_repository.h"
#include "controller_response.h"
#include "redis_hash_client.h"

#include <cstdint>
#include <string>

namespace navcaster::http_api
{

class OperationsController
{
public:
    OperationsController(storage::RedisHashClient &redis, std::int64_t now);

    ControllerResponse session_subject(const std::string &username) const;

    ControllerResponse list_accounts();
    ControllerResponse get_account(const std::string &account_id);
    ControllerResponse create_account(const std::string &body_text);
    ControllerResponse update_account(const std::string &account_id, const std::string &body_text);
    ControllerResponse delete_account(const std::string &account_id);

    ControllerResponse list_mount_point_groups();
    ControllerResponse create_mount_point_group(const std::string &body_text);
    ControllerResponse add_mount_point_group_member(const std::string &group_id, const std::string &body_text);
    ControllerResponse list_mount_points();
    ControllerResponse create_mount_point(const std::string &body_text);
    ControllerResponse create_mount_point(const std::string &mountpoint, const std::string &body_text);
    ControllerResponse grant_account_group(const std::string &account_id, const std::string &body_text);

    ControllerResponse list_access_accounts();
    ControllerResponse list_subscriptions();
    ControllerResponse create_subscription(const std::string &body_text);
    ControllerResponse append_balance_adjustment(const std::string &account_id, const std::string &body_text);

    ControllerResponse list_stations();
    ControllerResponse list_usage(const std::string &period);
    ControllerResponse list_supply_usage(const std::string &period);

private:
    ControllerResponse repository_result(int success_status, const storage::AccountDomainResult &result) const;
    ControllerResponse empty_or_records(const char *key) const;
    ControllerResponse list_limited_hash(const char *key) const;

    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
