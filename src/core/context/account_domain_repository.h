#pragma once

#include "redis_hash_client.h"
#include "repository_status.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

struct AccountDomainResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    std::string id;
    std::string error;
    nlohmann::json record;
};

class AccountDomainRepository
{
public:
    explicit AccountDomainRepository(RedisHashClient &redis);

    AccountDomainResult create_account(nlohmann::json record, std::int64_t now);
    AccountDomainResult get_account(const std::string &account_id);
    AccountDomainResult update_account(const std::string &account_id, nlohmann::json record, std::int64_t now);
    AccountDomainResult delete_account(const std::string &account_id, std::int64_t now);

    AccountDomainResult create_mount_point_group(nlohmann::json record, std::int64_t now);
    AccountDomainResult add_mount_point_group_member(const std::string &group_id, nlohmann::json member, std::int64_t now);
    AccountDomainResult grant_account_group(const std::string &account_id, nlohmann::json grant, std::int64_t now);
    AccountDomainResult create_mount_point(nlohmann::json record, std::int64_t now);

    AccountDomainResult create_access_account(nlohmann::json record, std::int64_t now);
    AccountDomainResult get_access_account(const std::string &access_account_id);
    AccountDomainResult update_access_account(const std::string &access_account_id, nlohmann::json record, std::int64_t now);
    AccountDomainResult delete_access_account(const std::string &access_account_id, std::int64_t now);

    AccountDomainResult create_subscription_plan(nlohmann::json record, std::int64_t now);
    AccountDomainResult get_subscription_plan(const std::string &plan_id);
    AccountDomainResult update_subscription_plan(const std::string &plan_id, nlohmann::json record, std::int64_t now);
    AccountDomainResult delete_subscription_plan(const std::string &plan_id, std::int64_t now);
    AccountDomainResult create_subscription(nlohmann::json record, std::int64_t now);
    AccountDomainResult get_subscription(const std::string &subscription_id);
    AccountDomainResult update_subscription(const std::string &subscription_id, nlohmann::json record, std::int64_t now);
    AccountDomainResult delete_subscription(const std::string &subscription_id, std::int64_t now);
    AccountDomainResult create_redeem_code(nlohmann::json record, std::int64_t now);
    AccountDomainResult redeem_code(const std::string &code, const std::string &account_id, nlohmann::json request, const std::string &period, std::int64_t now);
    AccountDomainResult upsert_station_record(nlohmann::json record, std::int64_t now);
    AccountDomainResult append_station_event(nlohmann::json event, std::int64_t now);

    AccountDomainResult append_balance_ledger(nlohmann::json entry, const std::string &period, std::int64_t now);
    AccountDomainResult apply_balance_adjustment(const std::string &account_id, nlohmann::json entry, const std::string &period, std::int64_t now);
    AccountDomainResult append_billing_usage(nlohmann::json entry, const std::string &period, std::int64_t now);
    AccountDomainResult get_data_push_config(const std::string &config_id);
    AccountDomainResult create_data_push_config(nlohmann::json record, std::int64_t now);
    AccountDomainResult update_data_push_config(const std::string &config_id, nlohmann::json record, std::int64_t now);
    AccountDomainResult delete_data_push_config(const std::string &config_id, std::int64_t now);
    AccountDomainResult append_data_push_usage(nlohmann::json entry, const std::string &period, std::int64_t now);
    AccountDomainResult append_data_push_usage_with_balance(nlohmann::json entry, const std::string &period, std::int64_t now);
    AccountDomainResult create_data_push_job(nlohmann::json request, const std::string &period, std::int64_t now);
    AccountDomainResult update_data_push_job_control(const std::string &job_id,
                                                     const std::string &period,
                                                     nlohmann::json request,
                                                     std::int64_t now);
    AccountDomainResult reconcile_data_push_job_runtime(const std::string &job_id,
                                                        const std::string &period,
                                                        nlohmann::json request,
                                                        std::int64_t now);
    AccountDomainResult reconcile_data_push_jobs_runtime(const std::string &period, std::int64_t now);
    AccountDomainResult get_data_push_maintenance_config(std::int64_t now);
    AccountDomainResult update_data_push_maintenance_config(nlohmann::json request, std::int64_t now);
    AccountDomainResult get_operations_alert_policy(std::int64_t now);
    AccountDomainResult update_operations_alert_policy(nlohmann::json request, std::int64_t now);
    AccountDomainResult maintain_data_push_jobs_runtime(const std::string &period,
                                                        nlohmann::json request,
                                                        std::int64_t now);
    AccountDomainResult append_supplier_supply_usage(nlohmann::json entry, const std::string &period, std::int64_t now);
    AccountDomainResult create_supplier_settlement(nlohmann::json request, const std::string &period, std::int64_t now);
    AccountDomainResult update_supplier_settlement_payment(const std::string &settlement_id,
                                                           const std::string &supplier_account_id,
                                                           const std::string &period,
                                                           nlohmann::json request,
                                                           std::int64_t now);

private:
    AccountDomainResult make_result(RepositoryStatus status, std::string id, std::string error) const;
    AccountDomainResult invalid(const std::string &message) const;
    AccountDomainResult redis_error(const std::string &id, const std::string &message) const;

    nlohmann::json get_hash_record(const char *key, const std::string &field);
    bool hset_json(const char *key, const std::string &field, const nlohmann::json &record);
    bool hsetnx_json(const char *key, const std::string &field, const nlohmann::json &record);

    bool account_has_group(const std::string &account_id, const std::string &group_id);
    bool group_is_active(const std::string &group_id);
    bool sync_legacy_access_group(const nlohmann::json &group, std::int64_t now);
    bool sync_legacy_access_item(const std::string &group_id, const nlohmann::json &member);
    void refresh_owner_access_indexes(const nlohmann::json &owner, std::int64_t now, const std::string &reason = {});
    void publish_access_status_update(const std::string &username, const std::string &reason);

    RedisHashClient &_redis;
};

} // namespace navcaster::storage
