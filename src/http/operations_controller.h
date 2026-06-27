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
    ControllerResponse list_subscription_plans();
    ControllerResponse get_subscription_plan(const std::string &plan_id);
    ControllerResponse create_subscription_plan(const std::string &body_text);
    ControllerResponse update_subscription_plan(const std::string &plan_id, const std::string &body_text);
    ControllerResponse delete_subscription_plan(const std::string &plan_id);
    ControllerResponse list_subscriptions();
    ControllerResponse get_subscription(const std::string &subscription_id);
    ControllerResponse create_subscription(const std::string &body_text);
    ControllerResponse update_subscription(const std::string &subscription_id, const std::string &body_text);
    ControllerResponse delete_subscription(const std::string &subscription_id);
    ControllerResponse append_balance_adjustment(const std::string &account_id, const std::string &body_text);
    ControllerResponse list_redeem_codes();
    ControllerResponse get_redeem_code(const std::string &code);
    ControllerResponse create_redeem_code(const std::string &body_text);
    ControllerResponse redeem_code(const std::string &code, const std::string &account_id, const std::string &body_text);

    ControllerResponse list_stations();
    ControllerResponse operations_monitor(const std::string &period);
    ControllerResponse operations_alert_policy();
    ControllerResponse update_operations_alert_policy(const std::string &body_text);
    ControllerResponse list_usage(const std::string &period);
    ControllerResponse list_data_push_configs();
    ControllerResponse get_data_push_config(const std::string &config_id);
    ControllerResponse create_data_push_config(const std::string &body_text);
    ControllerResponse update_data_push_config(const std::string &config_id, const std::string &body_text);
    ControllerResponse delete_data_push_config(const std::string &config_id);
    ControllerResponse list_data_push_jobs(const std::string &period);
    ControllerResponse update_data_push_job_control(const std::string &job_id, const std::string &period, const std::string &body_text);
    ControllerResponse reconcile_data_push_job_runtime(const std::string &job_id, const std::string &period, const std::string &body_text);
    ControllerResponse reconcile_data_push_jobs_runtime(const std::string &period);
    ControllerResponse data_push_maintenance_config();
    ControllerResponse update_data_push_maintenance_config(const std::string &body_text);
    ControllerResponse run_data_push_maintenance(const std::string &period, const std::string &body_text);
    ControllerResponse scheduled_data_push_maintenance(const std::string &period, std::int64_t last_run_time);
    ControllerResponse maintain_data_push_jobs_runtime(const std::string &period, std::int64_t unhealthy_after_seconds);
    ControllerResponse list_data_push_usage(const std::string &period);
    ControllerResponse list_supply_usage(const std::string &period);
    ControllerResponse list_supplier_settlements(const std::string &period, const std::string &supplier_account_id);
    ControllerResponse get_supplier_settlement(const std::string &settlement_id,
                                               const std::string &period,
                                               const std::string &supplier_account_id);
    ControllerResponse create_supplier_settlement(const std::string &body_text);
    ControllerResponse update_supplier_settlement_payment(const std::string &settlement_id,
                                                          const std::string &period,
                                                          const std::string &supplier_account_id,
                                                          const std::string &body_text);

private:
    ControllerResponse repository_result(int success_status, const storage::AccountDomainResult &result) const;
    ControllerResponse empty_or_records(const char *key) const;
    ControllerResponse list_limited_hash(const char *key) const;

    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
