#pragma once

#include "auth_session_service.h"
#include "account_domain_repository.h"
#include "controller_response.h"
#include "redis_hash_client.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

class SelfServiceController
{
public:
    SelfServiceController(storage::RedisHashClient &redis, std::int64_t now);

    ControllerResponse session_subject(const AuthSessionSubject &subject);

    ControllerResponse profile(const AuthSessionSubject &subject, const std::string &scope);
    ControllerResponse dashboard(const AuthSessionSubject &subject, const std::string &scope);
    ControllerResponse allowed_groups(const AuthSessionSubject &subject);
    ControllerResponse mount_points(const AuthSessionSubject &subject);

    ControllerResponse list_access_accounts(const AuthSessionSubject &subject, const std::string &scope);
    ControllerResponse get_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id);
    ControllerResponse create_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &body_text);
    ControllerResponse update_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id, const std::string &body_text);
    ControllerResponse update_access_account_password(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id, const std::string &body_text);
    ControllerResponse delete_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id);

    ControllerResponse usage(const AuthSessionSubject &subject, const std::string &period);
    ControllerResponse subscriptions(const AuthSessionSubject &subject);
    ControllerResponse redeem_redemptions(const AuthSessionSubject &subject);
    ControllerResponse data_push_configs(const AuthSessionSubject &subject);
    ControllerResponse data_push_jobs(const AuthSessionSubject &subject, const std::string &period);
    ControllerResponse create_data_push_job(const AuthSessionSubject &subject, const std::string &body_text);
    ControllerResponse data_push_usage(const AuthSessionSubject &subject, const std::string &period);
    ControllerResponse append_data_push_usage(const AuthSessionSubject &subject, const std::string &body_text);
    ControllerResponse supplier_stations(const AuthSessionSubject &subject);
    ControllerResponse supplier_supply_usage(const AuthSessionSubject &subject, const std::string &period);
    ControllerResponse supplier_settlements(const AuthSessionSubject &subject, const std::string &period);
    ControllerResponse supplier_earnings(const AuthSessionSubject &subject, const std::string &period);

private:
    ControllerResponse subject_error(const AuthSessionSubject &subject, const std::string &scope) const;
    bool scope_allowed(const AuthSessionSubject &subject, const std::string &scope) const;
    std::string expected_kind(const AuthSessionSubject &subject, const std::string &scope) const;
    nlohmann::json owner_account(const AuthSessionSubject &subject) const;
    bool owns_access_account(const AuthSessionSubject &subject, const nlohmann::json &record) const;

    ControllerResponse repository_result(int success_status, const storage::AccountDomainResult &result) const;
    nlohmann::json owner_access_accounts(const AuthSessionSubject &subject, const std::string &scope) const;
    nlohmann::json owner_group_grants(const AuthSessionSubject &subject) const;
    nlohmann::json visible_mount_points(const AuthSessionSubject &subject) const;
    nlohmann::json filter_billing_usage(const std::string &account_id, const std::string &period) const;
    nlohmann::json owner_subscriptions(const std::string &account_id) const;
    nlohmann::json owner_redeem_redemptions(const std::string &account_id) const;
    nlohmann::json active_data_push_configs() const;
    nlohmann::json filter_data_push_jobs(const std::string &account_id, const std::string &period) const;
    nlohmann::json filter_data_push_usage(const std::string &account_id, const std::string &period) const;
    nlohmann::json filter_supply_usage(const std::string &account_id, const std::string &period) const;
    nlohmann::json supplier_settlement_records(const std::string &account_id, const std::string &period) const;

    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
