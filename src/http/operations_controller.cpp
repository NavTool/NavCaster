#include "operations_controller.h"

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
    auto result = repo.append_balance_ledger(std::move(body), period, _now);
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

} // namespace navcaster::http_api
