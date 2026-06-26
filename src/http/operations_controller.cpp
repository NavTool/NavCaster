#include "operations_controller.h"

#include "account_domain.h"
#include "account_domain_repository.h"
#include "controller_helpers.h"
#include "json_record.h"
#include "redis_keys.h"

#include <string>
#include <utility>

namespace navcaster::http_api
{
namespace
{
nlohmann::json sanitized_record(nlohmann::json record)
{
    if (!record.is_object())
    {
        return record;
    }
    static constexpr const char *sensitive_fields[] = {
        "password",
        "old_password",
        "password_hash",
        "password_algo",
        "password_salt",
        "password_iterations",
    };
    for (const auto *field : sensitive_fields)
    {
        record.erase(field);
    }
    return record;
}

nlohmann::json sanitized_collection(const nlohmann::json &records)
{
    if (!records.is_object())
    {
        return nlohmann::json::object();
    }
    nlohmann::json result = nlohmann::json::object();
    for (auto it = records.begin(); it != records.end(); ++it)
    {
        result[it.key()] = sanitized_record(it.value());
    }
    return result;
}

bool parse_body_object(const std::string &body_text, nlohmann::json &body)
{
    return parse_json_body(body_text, body) && body.is_object();
}

std::string request_period(const std::string &period)
{
    return period.empty() ? "current" : period;
}

bool is_supplier_settlement_owner(const nlohmann::json &account)
{
    const std::string role = account.value("role", std::string{});
    return role == account_domain::ROLE_SUPPLIER || role == account_domain::ROLE_ADMIN;
}
} // namespace

OperationsController::OperationsController(storage::RedisHashClient &redis, std::int64_t now)
    : _redis(redis), _now(now)
{
}

ControllerResponse OperationsController::session_subject(const std::string &username) const
{
    return json_response(200, {
        {"username", username},
        {"role", "admin"},
        {"account_id", ""},
        {"compat_admin", true},
    });
}

ControllerResponse OperationsController::repository_result(int success_status, const storage::AccountDomainResult &result) const
{
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(success_status, sanitized_record(result.record));
}

ControllerResponse OperationsController::empty_or_records(const char *key) const
{
    return json_response(200, sanitized_collection(_redis.hgetall(key)));
}

ControllerResponse OperationsController::list_limited_hash(const char *key) const
{
    return empty_or_records(key);
}

ControllerResponse OperationsController::list_accounts()
{
    return empty_or_records(redis_keys::ACC_RECORD);
}

ControllerResponse OperationsController::get_account(const std::string &account_id)
{
    if (account_id.empty())
    {
        return error_response(400, "account_id is required");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.get_account(account_id);
    return repository_result(200, result);
}

ControllerResponse OperationsController::create_account(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_account(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::update_account(const std::string &account_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_account(account_id, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::delete_account(const std::string &account_id)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.delete_account(account_id, _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::list_mount_point_groups()
{
    return empty_or_records(redis_keys::MPGRP_RECORD);
}

ControllerResponse OperationsController::create_mount_point_group(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_mount_point_group(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::add_mount_point_group_member(const std::string &group_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.add_mount_point_group_member(group_id, std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::list_mount_points()
{
    return empty_or_records(redis_keys::MOUNT_RECORD);
}

ControllerResponse OperationsController::create_mount_point(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_mount_point(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::create_mount_point(const std::string &mountpoint, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["mountpoint"] = mountpoint;
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_mount_point(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::grant_account_group(const std::string &account_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.grant_account_group(account_id, std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::list_access_accounts()
{
    return empty_or_records(redis_keys::AACC_RECORD);
}

ControllerResponse OperationsController::list_subscriptions()
{
    return empty_or_records(redis_keys::SUB_RECORD);
}

ControllerResponse OperationsController::get_subscription(const std::string &subscription_id)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.get_subscription(subscription_id);
    return repository_result(200, result);
}

ControllerResponse OperationsController::create_subscription(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_subscription(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::update_subscription(const std::string &subscription_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_subscription(subscription_id, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::delete_subscription(const std::string &subscription_id)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.delete_subscription(subscription_id, _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::append_balance_adjustment(const std::string &account_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["account_id"] = account_id;
    if (!body.contains("ledger_id"))
    {
        return error_response(400, "ledger_id is required");
    }
    const std::string period = body.value("period", std::string("manual"));
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.apply_balance_adjustment(account_id, std::move(body), period, _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::list_redeem_codes()
{
    return empty_or_records(redis_keys::REDEEM_CODE);
}

ControllerResponse OperationsController::get_redeem_code(const std::string &code)
{
    const auto record = _redis.hget(redis_keys::REDEEM_CODE, code.c_str());
    if (!record.is_object())
    {
        return error_response(404, "RedeemCode not found");
    }
    return json_response(200, sanitized_record(record));
}

ControllerResponse OperationsController::create_redeem_code(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_redeem_code(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::redeem_code(const std::string &code, const std::string &account_id, const std::string &body_text)
{
    nlohmann::json body = nlohmann::json::object();
    if (!body_text.empty() && !parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    const std::string target_account_id = account_id.empty() ? body.value("account_id", std::string{}) : account_id;
    if (target_account_id.empty())
    {
        return error_response(400, "account_id is required");
    }
    const std::string period = body.value("period", std::string("manual"));
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.redeem_code(code, target_account_id, std::move(body), period, _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::list_stations()
{
    return empty_or_records(redis_keys::STATION_RECORD);
}

ControllerResponse OperationsController::list_usage(const std::string &period)
{
    const std::string key = redis_keys::bill_entry(request_period(period));
    return list_limited_hash(key.c_str());
}

ControllerResponse OperationsController::list_data_push_configs()
{
    return empty_or_records(redis_keys::DATA_PUSH_CONFIG);
}

ControllerResponse OperationsController::get_data_push_config(const std::string &config_id)
{
    if (config_id.empty())
    {
        return error_response(400, "config_id is required");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.get_data_push_config(config_id);
    return repository_result(200, result);
}

ControllerResponse OperationsController::create_data_push_config(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_data_push_config(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::update_data_push_config(const std::string &config_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_data_push_config(config_id, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::delete_data_push_config(const std::string &config_id)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.delete_data_push_config(config_id, _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::list_data_push_jobs(const std::string &period)
{
    const std::string key = redis_keys::data_push_job(request_period(period));
    return list_limited_hash(key.c_str());
}

ControllerResponse OperationsController::list_data_push_usage(const std::string &period)
{
    const std::string key = redis_keys::data_push(request_period(period));
    return list_limited_hash(key.c_str());
}

ControllerResponse OperationsController::list_supply_usage(const std::string &period)
{
    const std::string key = redis_keys::supply_usage(request_period(period));
    return list_limited_hash(key.c_str());
}

ControllerResponse OperationsController::list_supplier_settlements(const std::string &period, const std::string &supplier_account_id)
{
    const std::string resolved_period = request_period(period);
    if (!supplier_account_id.empty())
    {
        const std::string key = redis_keys::supply_earning(supplier_account_id, resolved_period);
        return list_limited_hash(key.c_str());
    }

    const auto accounts = _redis.hgetall(redis_keys::ACC_RECORD);
    nlohmann::json records = nlohmann::json::object();
    if (!accounts.is_object())
    {
        return json_response(200, records);
    }
    for (const auto &account : accounts)
    {
        if (!account.is_object() || !is_supplier_settlement_owner(account))
        {
            continue;
        }
        const std::string account_id = account.value("account_id", std::string{});
        if (account_id.empty())
        {
            continue;
        }
        const std::string key = redis_keys::supply_earning(account_id, resolved_period);
        const auto settlements = _redis.hgetall(key.c_str());
        if (!settlements.is_object())
        {
            continue;
        }
        for (auto it = settlements.begin(); it != settlements.end(); ++it)
        {
            records[it.key()] = sanitized_record(it.value());
        }
    }
    return json_response(200, records);
}

ControllerResponse OperationsController::get_supplier_settlement(const std::string &settlement_id,
                                                                 const std::string &period,
                                                                 const std::string &supplier_account_id)
{
    if (settlement_id.empty())
    {
        return error_response(400, "settlement_id is required");
    }
    const std::string resolved_period = request_period(period);
    if (!supplier_account_id.empty())
    {
        const std::string key = redis_keys::supply_earning(supplier_account_id, resolved_period);
        const auto record = _redis.hget(key.c_str(), settlement_id.c_str());
        if (!record.is_object())
        {
            return error_response(404, "SupplierSettlement not found");
        }
        return json_response(200, sanitized_record(record));
    }

    const auto accounts = _redis.hgetall(redis_keys::ACC_RECORD);
    if (accounts.is_object())
    {
        for (const auto &account : accounts)
        {
            if (!account.is_object() || !is_supplier_settlement_owner(account))
            {
                continue;
            }
            const std::string account_id = account.value("account_id", std::string{});
            if (account_id.empty())
            {
                continue;
            }
            const std::string key = redis_keys::supply_earning(account_id, resolved_period);
            const auto record = _redis.hget(key.c_str(), settlement_id.c_str());
            if (record.is_object())
            {
                return json_response(200, sanitized_record(record));
            }
        }
    }
    return error_response(404, "SupplierSettlement not found");
}

ControllerResponse OperationsController::create_supplier_settlement(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    const std::string period = request_period(body.value("period", std::string{}));
    body["period"] = period;
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_supplier_settlement(std::move(body), period, _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::update_supplier_settlement_payment(const std::string &settlement_id,
                                                                            const std::string &period,
                                                                            const std::string &supplier_account_id,
                                                                            const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    const std::string resolved_period = request_period(body.value("period", period));
    const std::string resolved_supplier = body.value("supplier_account_id", supplier_account_id);
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_supplier_settlement_payment(settlement_id,
                                                          resolved_supplier,
                                                          resolved_period,
                                                          std::move(body),
                                                          _now);
    return repository_result(200, result);
}

} // namespace navcaster::http_api
