#include "operations_controller.h"

#include "account_domain.h"
#include "account_domain_repository.h"
#include "controller_helpers.h"
#include "json_record.h"
#include "redis_keys.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace navcaster::http_api
{
namespace
{
constexpr std::size_t MAX_RISK_ACCOUNTS = 12;
constexpr std::size_t MAX_RECENT_FAILED_JOBS = 8;
constexpr std::size_t MAX_RECENT_ALERT_EVENTS = 8;

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
        "target_password",
        "relay_target_password",
    };
    for (const auto *field : sensitive_fields)
    {
        record.erase(field);
    }
    for (auto it = record.begin(); it != record.end(); ++it)
    {
        if (it.value().is_object())
        {
            it.value() = sanitized_record(it.value());
        }
        else if (it.value().is_array())
        {
            for (auto &item : it.value())
            {
                if (item.is_object())
                {
                    item = sanitized_record(item);
                }
            }
        }
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

std::string string_value(const nlohmann::json &record, const char *field, std::string fallback = {})
{
    return json_record::string_field(record, field, std::move(fallback));
}

std::int64_t i64_value(const nlohmann::json &record, const char *field, std::int64_t fallback = 0)
{
    const auto it = record.find(field);
    return it == record.end() ? fallback : json_record::as_i64(*it, fallback);
}

bool bool_value(const nlohmann::json &record, const char *field, bool fallback = false)
{
    const auto it = record.find(field);
    if (it == record.end())
    {
        return fallback;
    }
    if (it->is_boolean())
    {
        return it->get<bool>();
    }
    return json_record::bool_or_number_as_int(*it, fallback ? 1 : 0) != 0;
}

bool is_expired(const nlohmann::json &record, std::int64_t now)
{
    const auto expire_time = i64_value(record, "expire_time", 0);
    return expire_time > 0 && now > 0 && expire_time <= now;
}

void increment_json_count(nlohmann::json &record, const char *field, std::int64_t delta = 1)
{
    record[field] = json_record::as_i64(record.value(field, 0), 0) + delta;
}

void increment_status_count(nlohmann::json &record, const std::string &status)
{
    const auto key = status.empty() ? std::string("other") : status;
    record[key] = json_record::as_i64(record.value(key, 0), 0) + 1;
}

void append_alert(nlohmann::json &alerts,
                  const std::string &severity,
                  const std::string &code,
                  std::int64_t count,
                  std::int64_t threshold,
                  const std::string &message)
{
    if (count < threshold)
    {
        return;
    }
    alerts.push_back({
        {"severity", severity},
        {"code", code},
        {"count", count},
        {"threshold", threshold},
        {"message", message},
    });
}

bool alert_enabled(const nlohmann::json &policy, const char *field)
{
    return bool_value(policy, "enabled", true) && bool_value(policy, field, true);
}

std::string operations_alert_event_id(const std::string &period, const std::string &code)
{
    return "opsalert:" + period + ":" + code;
}

std::string period_from_alert_event_id(const std::string &alert_event_id)
{
    const std::string prefix = "opsalert:";
    if (alert_event_id.rfind(prefix, 0) != 0)
    {
        return {};
    }
    const auto start = prefix.size();
    const auto end = alert_event_id.find(':', start);
    if (end == std::string::npos || end == start)
    {
        return {};
    }
    return alert_event_id.substr(start, end - start);
}

std::int64_t alert_event_sort_time(const nlohmann::json &event)
{
    const auto last_seen_time = i64_value(event, "last_seen_time", 0);
    if (last_seen_time > 0)
    {
        return last_seen_time;
    }
    const auto update_time = i64_value(event, "update_time", 0);
    return update_time > 0 ? update_time : i64_value(event, "create_time", 0);
}

nlohmann::json operations_alert_events_summary(storage::RedisHashClient &redis, const std::string &period)
{
    const auto records = redis.hgetall(redis_keys::operations_alert_event(period).c_str());
    nlohmann::json summary = {
        {"period", period},
        {"total_count", 0},
        {"open_count", 0},
        {"acknowledged_count", 0},
        {"resolved_count", 0},
        {"recent_events", nlohmann::json::array()},
    };
    if (!records.is_object())
    {
        return summary;
    }

    std::vector<nlohmann::json> recent;
    for (const auto &event : records)
    {
        if (!event.is_object())
        {
            continue;
        }
        increment_json_count(summary, "total_count");
        const std::string status = string_value(event, "status", "open");
        if (status == "acknowledged")
        {
            increment_json_count(summary, "acknowledged_count");
        }
        else if (status == "resolved")
        {
            increment_json_count(summary, "resolved_count");
        }
        else
        {
            increment_json_count(summary, "open_count");
        }
        recent.push_back(sanitized_record(event));
    }
    std::sort(recent.begin(), recent.end(), [](const auto &lhs, const auto &rhs) {
        return alert_event_sort_time(lhs) > alert_event_sort_time(rhs);
    });
    for (const auto &event : recent)
    {
        if (summary["recent_events"].size() >= MAX_RECENT_ALERT_EVENTS)
        {
            break;
        }
        summary["recent_events"].push_back(event);
    }
    return summary;
}

nlohmann::json upsert_operations_alert_events(storage::RedisHashClient &redis,
                                              const std::string &period,
                                              const nlohmann::json &alerts,
                                              std::int64_t now,
                                              bool *redis_ok)
{
    nlohmann::json events = nlohmann::json::object();
    if (redis_ok)
    {
        *redis_ok = true;
    }
    if (!alerts.is_array())
    {
        return events;
    }

    const std::string key = redis_keys::operations_alert_event(period);
    for (const auto &alert : alerts)
    {
        if (!alert.is_object())
        {
            continue;
        }
        const std::string code = string_value(alert, "code");
        if (code.empty())
        {
            continue;
        }
        const std::string event_id = operations_alert_event_id(period, code);
        auto event = redis.hget(key.c_str(), event_id.c_str());
        if (!event.is_object())
        {
            event = {
                {"alert_event_id", event_id},
                {"period", period},
                {"code", code},
                {"status", "open"},
                {"first_seen_time", now},
                {"create_time", now},
                {"occurrence_count", 0},
                {"source", "operations_monitor"},
            };
        }
        const std::string current_status = string_value(event, "status", "open");
        if (current_status == "resolved")
        {
            event["status"] = "open";
            event["reopen_time"] = now;
            event["reopen_count"] = i64_value(event, "reopen_count", 0) + 1;
        }
        else if (current_status.empty())
        {
            event["status"] = "open";
        }
        event["severity"] = string_value(alert, "severity", "warning");
        event["count"] = i64_value(alert, "count", 0);
        event["threshold"] = i64_value(alert, "threshold", 1);
        event["message"] = string_value(alert, "message");
        event["last_seen_time"] = now;
        event["update_time"] = now;
        event["occurrence_count"] = i64_value(event, "occurrence_count", 0) + 1;

        if (!redis.hset(key.c_str(), event_id.c_str(), json_record::dump_record(event)))
        {
            if (redis_ok)
            {
                *redis_ok = false;
            }
            break;
        }
        events[event_id] = sanitized_record(event);
    }
    return events;
}

std::int64_t data_push_job_sort_time(const nlohmann::json &job)
{
    const auto failure_time = i64_value(job, "failure_time", 0);
    if (failure_time > 0)
    {
        return failure_time;
    }
    const auto maintenance_time = i64_value(job, "runtime_maintenance_time", 0);
    if (maintenance_time > 0)
    {
        return maintenance_time;
    }
    const auto update_time = i64_value(job, "update_time", 0);
    return update_time > 0 ? update_time : i64_value(job, "create_time", 0);
}

nlohmann::json data_push_job_monitor_summary(const nlohmann::json &job, const std::string &period)
{
    return sanitized_record({
        {"job_id", string_value(job, "job_id")},
        {"period", string_value(job, "period", period)},
        {"account_id", string_value(job, "account_id")},
        {"config_id", string_value(job, "config_id")},
        {"target_mountpoint", string_value(job, "target_mountpoint")},
        {"execution_mode", string_value(job, "execution_mode")},
        {"relay_uid", string_value(job, "relay_uid")},
        {"relay_status", string_value(job, "relay_status")},
        {"status", string_value(job, "status")},
        {"failure_reason", string_value(job, "failure_reason")},
        {"failure_time", i64_value(job, "failure_time", 0)},
        {"runtime_maintenance_time", i64_value(job, "runtime_maintenance_time", 0)},
        {"update_time", i64_value(job, "update_time", 0)},
        {"create_time", i64_value(job, "create_time", 0)},
    });
}

bool starts_with(const std::string &text, const std::string &prefix)
{
    return text.rfind(prefix, 0) == 0;
}

bool ends_with(const std::string &text, const std::string &suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> supplier_settlement_keys(storage::RedisHashClient &redis,
                                                  const nlohmann::json &accounts,
                                                  const std::string &period)
{
    std::vector<std::string> keys;
    const std::string prefix = redis_keys::SUPPLY_EARNING_PREFIX;
    const std::string suffix = ":" + period;
    for (const auto &key : redis.scan_all_keys())
    {
        if (starts_with(key, prefix) && ends_with(key, suffix))
        {
            keys.push_back(key);
        }
    }
    if (!keys.empty() || !accounts.is_object())
    {
        return keys;
    }

    for (const auto &account : accounts)
    {
        if (!account.is_object())
        {
            continue;
        }
        const std::string role = string_value(account, "role");
        if (role != account_domain::ROLE_SUPPLIER && role != account_domain::ROLE_ADMIN)
        {
            continue;
        }
        const std::string account_id = string_value(account, "account_id");
        if (!account_id.empty())
        {
            keys.push_back(redis_keys::supply_earning(account_id, period));
        }
    }
    return keys;
}

nlohmann::json default_data_push_maintenance_snapshot(std::int64_t now)
{
    return {
        {"config_id", "default"},
        {"enabled", true},
        {"interval_seconds", 60},
        {"unhealthy_after_seconds", 300},
        {"period_scope", "current"},
        {"create_time", now},
        {"update_time", now},
    };
}

bool is_supplier_settlement_owner(const nlohmann::json &account)
{
    const std::string role = account.value("role", std::string{});
    return role == account_domain::ROLE_SUPPLIER || role == account_domain::ROLE_ADMIN;
}

std::string relay_status_text(const nlohmann::json &state)
{
    if (!state.is_object())
    {
        return "pending";
    }
    return json_record::as_i64(state.value("state", 0), 0) == 1 ? "running" : "stopped";
}

nlohmann::json data_push_job_with_runtime(nlohmann::json job, storage::RedisHashClient &redis)
{
    if (!job.is_object())
    {
        return job;
    }
    const std::string relay_uid = job.value("relay_uid", std::string{});
    if (relay_uid.empty())
    {
        return job;
    }
    const auto state = redis.hget(redis_keys::PUSH_STAT, relay_uid.c_str());
    job["relay_status"] = relay_status_text(state);
    if (state.is_object())
    {
        job["relay_state"] = state;
        if (json_record::as_i64(state.value("state", 0), 0) == 1 &&
            job.value("status", std::string{}) == "queued")
        {
            job["status"] = "running";
        }
    }
    return job;
}

nlohmann::json data_push_jobs_with_runtime(const nlohmann::json &records, storage::RedisHashClient &redis)
{
    if (!records.is_object())
    {
        return nlohmann::json::object();
    }
    nlohmann::json result = nlohmann::json::object();
    for (auto it = records.begin(); it != records.end(); ++it)
    {
        result[it.key()] = data_push_job_with_runtime(it.value(), redis);
    }
    return result;
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

ControllerResponse OperationsController::list_subscription_plans()
{
    return empty_or_records(redis_keys::SUB_PLAN);
}

ControllerResponse OperationsController::get_subscription_plan(const std::string &plan_id)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.get_subscription_plan(plan_id);
    return repository_result(200, result);
}

ControllerResponse OperationsController::create_subscription_plan(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_subscription_plan(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse OperationsController::update_subscription_plan(const std::string &plan_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_subscription_plan(plan_id, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::delete_subscription_plan(const std::string &plan_id)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.delete_subscription_plan(plan_id, _now);
    return repository_result(200, result);
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

ControllerResponse OperationsController::operations_monitor(const std::string &period)
{
    const std::string resolved_period = request_period(period);
    storage::AccountDomainRepository repo(_redis);
    auto policy_result = repo.get_operations_alert_policy(_now);
    nlohmann::json alert_policy = policy_result.status == storage::RepositoryStatus::Ok
                                      ? sanitized_record(policy_result.record)
                                      : nlohmann::json{{"policy_id", "default"}, {"enabled", false}, {"error", policy_result.error}};
    const auto low_balance_threshold_cents = i64_value(alert_policy, "low_balance_threshold_cents", 1000);
    const auto accounts_records = _redis.hgetall(redis_keys::ACC_RECORD);
    const auto subscription_records = _redis.hgetall(redis_keys::SUB_RECORD);
    const auto redeem_records = _redis.hgetall(redis_keys::REDEEM_CODE);
    const auto data_push_usage_records = _redis.hgetall(redis_keys::data_push(resolved_period).c_str());
    const auto data_push_job_records = data_push_jobs_with_runtime(
        _redis.hgetall(redis_keys::data_push_job(resolved_period).c_str()),
        _redis);
    const auto supply_usage_records = _redis.hgetall(redis_keys::supply_usage(resolved_period).c_str());

    nlohmann::json account_summary = {
        {"total_count", 0},
        {"admin_count", 0},
        {"user_count", 0},
        {"supplier_count", 0},
        {"active_count", 0},
        {"disabled_count", 0},
        {"frozen_count", 0},
        {"expired_count", 0},
        {"deleted_count", 0},
        {"negative_balance_count", 0},
        {"low_balance_count", 0},
        {"low_balance_threshold_cents", low_balance_threshold_cents},
        {"total_balance_cents", 0},
        {"risk_accounts", nlohmann::json::array()},
    };
    if (accounts_records.is_object())
    {
        for (const auto &account : accounts_records)
        {
            if (!account.is_object())
            {
                continue;
            }
            increment_json_count(account_summary, "total_count");
            const std::string role = string_value(account, "role");
            if (role == account_domain::ROLE_ADMIN)
            {
                increment_json_count(account_summary, "admin_count");
            }
            else if (role == account_domain::ROLE_SUPPLIER)
            {
                increment_json_count(account_summary, "supplier_count");
            }
            else if (role == account_domain::ROLE_USER)
            {
                increment_json_count(account_summary, "user_count");
            }

            const std::string status = string_value(account, "status", account_domain::STATUS_ACTIVE);
            const bool expired = is_expired(account, _now) || status == "expired";
            if (status == account_domain::STATUS_ACTIVE && !expired)
            {
                increment_json_count(account_summary, "active_count");
            }
            if (status == account_domain::STATUS_DISABLED)
            {
                increment_json_count(account_summary, "disabled_count");
            }
            if (status == "frozen")
            {
                increment_json_count(account_summary, "frozen_count");
            }
            if (status == account_domain::STATUS_DELETED)
            {
                increment_json_count(account_summary, "deleted_count");
            }
            if (expired)
            {
                increment_json_count(account_summary, "expired_count");
            }

            const auto balance_cents = i64_value(account, "balance_cents", 0);
            account_summary["total_balance_cents"] = json_record::as_i64(account_summary["total_balance_cents"], 0) + balance_cents;
            nlohmann::json risks = nlohmann::json::array();
            if (status != account_domain::STATUS_DELETED && balance_cents < 0)
            {
                increment_json_count(account_summary, "negative_balance_count");
                risks.push_back("negative_balance");
            }
            else if (status != account_domain::STATUS_DELETED && balance_cents < low_balance_threshold_cents)
            {
                increment_json_count(account_summary, "low_balance_count");
                risks.push_back("low_balance");
            }
            if (status == account_domain::STATUS_DISABLED)
            {
                risks.push_back("disabled");
            }
            if (status == "frozen")
            {
                risks.push_back("frozen");
            }
            if (expired)
            {
                risks.push_back("expired");
            }
            if (!risks.empty() && account_summary["risk_accounts"].size() < MAX_RISK_ACCOUNTS)
            {
                account_summary["risk_accounts"].push_back(sanitized_record({
                    {"account_id", string_value(account, "account_id")},
                    {"username", string_value(account, "username")},
                    {"role", role},
                    {"status", status},
                    {"balance_cents", balance_cents},
                    {"expire_time", i64_value(account, "expire_time", 0)},
                    {"risks", risks},
                }));
            }
        }
    }

    nlohmann::json subscription_summary = {
        {"total_count", 0},
        {"active_count", 0},
        {"disabled_count", 0},
        {"expired_count", 0},
        {"deleted_count", 0},
    };
    if (subscription_records.is_object())
    {
        for (const auto &subscription : subscription_records)
        {
            if (!subscription.is_object())
            {
                continue;
            }
            increment_json_count(subscription_summary, "total_count");
            const std::string status = string_value(subscription, "status", account_domain::STATUS_ACTIVE);
            const bool expired = is_expired(subscription, _now) || status == "expired";
            if (status == account_domain::STATUS_ACTIVE && !expired)
            {
                increment_json_count(subscription_summary, "active_count");
            }
            if (status == account_domain::STATUS_DISABLED)
            {
                increment_json_count(subscription_summary, "disabled_count");
            }
            if (status == account_domain::STATUS_DELETED)
            {
                increment_json_count(subscription_summary, "deleted_count");
            }
            if (expired)
            {
                increment_json_count(subscription_summary, "expired_count");
            }
        }
    }

    nlohmann::json redeem_summary = {
        {"total_count", 0},
        {"active_count", 0},
        {"disabled_count", 0},
        {"expired_count", 0},
        {"available_count", 0},
    };
    if (redeem_records.is_object())
    {
        for (const auto &code : redeem_records)
        {
            if (!code.is_object())
            {
                continue;
            }
            increment_json_count(redeem_summary, "total_count");
            const std::string status = string_value(code, "status", account_domain::STATUS_ACTIVE);
            const bool expired = is_expired(code, _now) || status == "expired";
            if (status == account_domain::STATUS_ACTIVE && !expired)
            {
                increment_json_count(redeem_summary, "active_count");
            }
            if (status == account_domain::STATUS_DISABLED)
            {
                increment_json_count(redeem_summary, "disabled_count");
            }
            if (expired)
            {
                increment_json_count(redeem_summary, "expired_count");
            }
            const auto redeemed_count = i64_value(code, "redeemed_count", 0);
            const auto max_redemptions = i64_value(code, "max_redemptions", 1);
            if (status == account_domain::STATUS_ACTIVE && !expired && redeemed_count < max_redemptions)
            {
                increment_json_count(redeem_summary, "available_count");
            }
        }
    }

    nlohmann::json data_push_summary = {
        {"period", resolved_period},
        {"usage_count", 0},
        {"total_used_seconds", 0},
        {"total_debit_cents", 0},
        {"job_count", 0},
        {"relay_push_count", 0},
        {"ledger_only_count", 0},
        {"status_counts", {
            {"queued", 0},
            {"running", 0},
            {"failed", 0},
            {"completed", 0},
            {"cancelled", 0},
            {"other", 0},
        }},
        {"failed_count", 0},
        {"running_count", 0},
        {"queued_count", 0},
        {"maintenance", default_data_push_maintenance_snapshot(_now)},
        {"recent_failed_jobs", nlohmann::json::array()},
    };
    if (data_push_usage_records.is_object())
    {
        for (const auto &usage : data_push_usage_records)
        {
            if (!usage.is_object())
            {
                continue;
            }
            increment_json_count(data_push_summary, "usage_count");
            data_push_summary["total_used_seconds"] = json_record::as_i64(data_push_summary["total_used_seconds"], 0) + i64_value(usage, "used_seconds", 0);
            data_push_summary["total_debit_cents"] = json_record::as_i64(data_push_summary["total_debit_cents"], 0) + i64_value(usage, "actual_debit_cents", 0);
        }
    }
    std::vector<nlohmann::json> failed_jobs;
    if (data_push_job_records.is_object())
    {
        for (const auto &job : data_push_job_records)
        {
            if (!job.is_object())
            {
                continue;
            }
            increment_json_count(data_push_summary, "job_count");
            const std::string mode = string_value(job, "execution_mode", account_domain::DATA_PUSH_EXECUTION_LEDGER_ONLY);
            if (mode == account_domain::DATA_PUSH_EXECUTION_RELAY_PUSH)
            {
                increment_json_count(data_push_summary, "relay_push_count");
            }
            else
            {
                increment_json_count(data_push_summary, "ledger_only_count");
            }
            const std::string status = string_value(job, "status", "other");
            if (status == "queued" || status == "running" || status == "failed" || status == "completed" || status == "cancelled")
            {
                increment_status_count(data_push_summary["status_counts"], status);
            }
            else
            {
                increment_status_count(data_push_summary["status_counts"], "other");
            }
            if (status == "failed")
            {
                failed_jobs.push_back(data_push_job_monitor_summary(job, resolved_period));
            }
        }
    }
    data_push_summary["failed_count"] = data_push_summary["status_counts"].value("failed", 0);
    data_push_summary["running_count"] = data_push_summary["status_counts"].value("running", 0);
    data_push_summary["queued_count"] = data_push_summary["status_counts"].value("queued", 0);
    std::sort(failed_jobs.begin(), failed_jobs.end(), [](const auto &lhs, const auto &rhs) {
        return data_push_job_sort_time(lhs) > data_push_job_sort_time(rhs);
    });
    for (const auto &job : failed_jobs)
    {
        if (data_push_summary["recent_failed_jobs"].size() >= MAX_RECENT_FAILED_JOBS)
        {
            break;
        }
        data_push_summary["recent_failed_jobs"].push_back(job);
    }

    auto maintenance = repo.get_data_push_maintenance_config(_now);
    if (maintenance.status == storage::RepositoryStatus::Ok)
    {
        data_push_summary["maintenance"] = sanitized_record(maintenance.record);
    }
    else
    {
        data_push_summary["maintenance"]["error"] = maintenance.error;
    }

    nlohmann::json supply_summary = {
        {"period", resolved_period},
        {"usage_count", 0},
        {"pending_usage_count", 0},
        {"settled_usage_count", 0},
        {"total_supply_seconds", 0},
        {"total_earning_cents", 0},
        {"pending_earning_cents", 0},
        {"settled_earning_cents", 0},
        {"settlements", {
            {"count", 0},
            {"pending_payment_count", 0},
            {"paid_count", 0},
            {"payment_failed_count", 0},
            {"cancelled_count", 0},
            {"other_count", 0},
            {"pending_payment_cents", 0},
            {"paid_cents", 0},
            {"failed_payment_cents", 0},
            {"cancelled_payment_cents", 0},
            {"other_cents", 0},
        }},
    };
    if (supply_usage_records.is_object())
    {
        for (const auto &usage : supply_usage_records)
        {
            if (!usage.is_object())
            {
                continue;
            }
            increment_json_count(supply_summary, "usage_count");
            const auto seconds = i64_value(usage, "used_seconds", 0);
            const auto cents = i64_value(usage, "earning_cents", 0);
            supply_summary["total_supply_seconds"] = json_record::as_i64(supply_summary["total_supply_seconds"], 0) + seconds;
            supply_summary["total_earning_cents"] = json_record::as_i64(supply_summary["total_earning_cents"], 0) + cents;
            const std::string status = string_value(usage, "status", "pending");
            if (status == "settled")
            {
                increment_json_count(supply_summary, "settled_usage_count");
                supply_summary["settled_earning_cents"] = json_record::as_i64(supply_summary["settled_earning_cents"], 0) + cents;
            }
            else
            {
                increment_json_count(supply_summary, "pending_usage_count");
                supply_summary["pending_earning_cents"] = json_record::as_i64(supply_summary["pending_earning_cents"], 0) + cents;
            }
        }
    }
    for (const auto &key : supplier_settlement_keys(_redis, accounts_records, resolved_period))
    {
        const auto settlement_records = _redis.hgetall(key.c_str());
        if (!settlement_records.is_object())
        {
            continue;
        }
        for (const auto &settlement : settlement_records)
        {
            if (!settlement.is_object())
            {
                continue;
            }
            increment_json_count(supply_summary["settlements"], "count");
            const auto cents = i64_value(settlement, "total_earning_cents", 0);
            const std::string status = string_value(settlement, "status", "pending_payment");
            if (status == "pending_payment" || status.empty())
            {
                increment_json_count(supply_summary["settlements"], "pending_payment_count");
                supply_summary["settlements"]["pending_payment_cents"] = json_record::as_i64(supply_summary["settlements"]["pending_payment_cents"], 0) + cents;
            }
            else if (status == "paid" || status == "settled")
            {
                increment_json_count(supply_summary["settlements"], "paid_count");
                supply_summary["settlements"]["paid_cents"] = json_record::as_i64(supply_summary["settlements"]["paid_cents"], 0) + cents;
            }
            else if (status == "payment_failed")
            {
                increment_json_count(supply_summary["settlements"], "payment_failed_count");
                supply_summary["settlements"]["failed_payment_cents"] = json_record::as_i64(supply_summary["settlements"]["failed_payment_cents"], 0) + cents;
            }
            else if (status == "cancelled" || status == "void")
            {
                increment_json_count(supply_summary["settlements"], "cancelled_count");
                supply_summary["settlements"]["cancelled_payment_cents"] = json_record::as_i64(supply_summary["settlements"]["cancelled_payment_cents"], 0) + cents;
            }
            else
            {
                increment_json_count(supply_summary["settlements"], "other_count");
                supply_summary["settlements"]["other_cents"] = json_record::as_i64(supply_summary["settlements"]["other_cents"], 0) + cents;
            }
        }
    }

    nlohmann::json alerts = nlohmann::json::array();
    if (alert_enabled(alert_policy, "negative_balance_enabled"))
    {
        append_alert(alerts, "critical", "negative_balance", account_summary.value("negative_balance_count", 0), 1, "Account balance is below zero");
    }
    if (alert_enabled(alert_policy, "low_balance_enabled"))
    {
        append_alert(alerts, "warning", "low_balance", account_summary.value("low_balance_count", 0), 1, "Account balance is below the operations threshold");
    }
    if (alert_enabled(alert_policy, "data_push_failed_enabled"))
    {
        append_alert(alerts,
                     "critical",
                     "data_push_failed",
                     data_push_summary.value("failed_count", 0),
                     i64_value(alert_policy, "data_push_failed_threshold", 1),
                     "DataPush jobs failed in the selected period");
    }
    if (alert_enabled(alert_policy, "data_push_maintenance_disabled_enabled") && !bool_value(data_push_summary["maintenance"], "enabled", true))
    {
        append_alert(alerts, "warning", "data_push_maintenance_disabled", 1, 1, "DataPush runtime maintenance is disabled");
    }
    if (alert_enabled(alert_policy, "supplier_pending_payment_enabled"))
    {
        append_alert(alerts,
                     "warning",
                     "supplier_pending_payment",
                     supply_summary["settlements"].value("pending_payment_count", 0),
                     i64_value(alert_policy, "supplier_pending_payment_threshold", 1),
                     "Supplier settlements are waiting for payment");
    }
    if (alert_enabled(alert_policy, "supplier_usage_pending_enabled"))
    {
        append_alert(alerts,
                     "warning",
                     "supplier_usage_pending",
                     supply_summary.value("pending_usage_count", 0),
                     i64_value(alert_policy, "supplier_usage_pending_threshold", 1),
                     "Supplier usage is not settled yet");
    }

    return json_response(200, {
        {"period", resolved_period},
        {"generated_time", _now},
        {"alert_policy", alert_policy},
        {"alert_events", operations_alert_events_summary(_redis, resolved_period)},
        {"accounts", account_summary},
        {"subscriptions", subscription_summary},
        {"redeem_codes", redeem_summary},
        {"data_push", data_push_summary},
        {"supply", supply_summary},
        {"alerts", alerts},
    });
}

ControllerResponse OperationsController::operations_alert_policy()
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.get_operations_alert_policy(_now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::update_operations_alert_policy(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_operations_alert_policy(std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::list_operations_alert_events(const std::string &period, const std::string &status)
{
    const std::string resolved_period = request_period(period);
    const auto records = _redis.hgetall(redis_keys::operations_alert_event(resolved_period).c_str());
    if (status.empty())
    {
        return json_response(200, sanitized_collection(records));
    }
    nlohmann::json filtered = nlohmann::json::object();
    if (records.is_object())
    {
        for (auto it = records.begin(); it != records.end(); ++it)
        {
            if (it.value().is_object() && string_value(it.value(), "status", "open") == status)
            {
                filtered[it.key()] = sanitized_record(it.value());
            }
        }
    }
    return json_response(200, filtered);
}

ControllerResponse OperationsController::sync_operations_alert_events(const std::string &period)
{
    const std::string resolved_period = request_period(period);
    auto monitor = operations_monitor(resolved_period);
    if (monitor.status_code != 200)
    {
        return monitor;
    }

    nlohmann::json body;
    try
    {
        body = nlohmann::json::parse(monitor.body);
    }
    catch (...)
    {
        return error_response(500, "Failed to parse operations monitor snapshot");
    }

    bool redis_ok = true;
    auto events = upsert_operations_alert_events(_redis, resolved_period, body.value("alerts", nlohmann::json::array()), _now, &redis_ok);
    if (!redis_ok)
    {
        return error_response(500, "Failed to persist operations alert events");
    }
    return json_response(200, {
        {"period", resolved_period},
        {"synced_count", static_cast<int>(events.size())},
        {"events", events},
    });
}

ControllerResponse OperationsController::update_operations_alert_event(const std::string &alert_event_id,
                                                                       const std::string &period,
                                                                       const std::string &action,
                                                                       const std::string &body_text)
{
    if (alert_event_id.empty())
    {
        return error_response(400, "alert_event_id is required");
    }
    if (action != "acknowledge" && action != "resolve")
    {
        return error_response(400, "action must be acknowledge or resolve");
    }
    nlohmann::json body = nlohmann::json::object();
    if (!body_text.empty() && !parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }

    const std::string id_period = period_from_alert_event_id(alert_event_id);
    const std::string resolved_period = request_period(body.value("period", period.empty() ? id_period : period));
    const std::string key = redis_keys::operations_alert_event(resolved_period);
    auto event = _redis.hget(key.c_str(), alert_event_id.c_str());
    if (!event.is_object())
    {
        return error_response(404, "OperationsAlertEvent not found");
    }
    const std::string current_status = string_value(event, "status", "open");
    const std::string actor = body.value("operator", body.value("actor", std::string("admin")));
    if (body.contains("operator_note"))
    {
        event["operator_note"] = body["operator_note"];
    }
    if (action == "acknowledge")
    {
        if (current_status == "resolved")
        {
            return error_response(409, "Resolved OperationsAlertEvent cannot be acknowledged");
        }
        event["status"] = "acknowledged";
        event["acknowledged_time"] = _now;
        event["acknowledged_by"] = actor;
    }
    else
    {
        event["status"] = "resolved";
        event["resolved_time"] = _now;
        event["resolved_by"] = actor;
    }
    event["update_time"] = _now;
    if (!_redis.hset(key.c_str(), alert_event_id.c_str(), json_record::dump_record(event)))
    {
        return error_response(500, "Failed to update operations alert event");
    }
    return json_response(200, sanitized_record(event));
}

ControllerResponse OperationsController::list_usage(const std::string &period)
{
    const std::string key = redis_keys::bill_entry(request_period(period));
    return list_limited_hash(key.c_str());
}

ControllerResponse OperationsController::list_runtime_rejections(const std::string &period)
{
    const std::string key = redis_keys::runtime_rejection(request_period(period));
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
    return json_response(200, sanitized_collection(data_push_jobs_with_runtime(_redis.hgetall(key.c_str()), _redis)));
}

ControllerResponse OperationsController::update_data_push_job_control(const std::string &job_id, const std::string &period, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["period"] = request_period(body.value("period", period));
    const std::string resolved_period = body.value("period", std::string("current"));
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_data_push_job_control(job_id, resolved_period, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::reconcile_data_push_job_runtime(const std::string &job_id, const std::string &period, const std::string &body_text)
{
    nlohmann::json body = nlohmann::json::object();
    if (!body_text.empty() && !parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["period"] = request_period(body.value("period", period));
    const std::string resolved_period = body.value("period", std::string("current"));
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.reconcile_data_push_job_runtime(job_id, resolved_period, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::reconcile_data_push_jobs_runtime(const std::string &period)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.reconcile_data_push_jobs_runtime(request_period(period), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::data_push_maintenance_config()
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.get_data_push_maintenance_config(_now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::update_data_push_maintenance_config(const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.update_data_push_maintenance_config(std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse OperationsController::run_data_push_maintenance(const std::string &period, const std::string &body_text)
{
    nlohmann::json body = nlohmann::json::object();
    if (!body_text.empty() && !parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }

    storage::AccountDomainRepository repo(_redis);
    auto config = repo.get_data_push_maintenance_config(_now);
    if (config.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(config.status, config.error);
    }

    const std::string resolved_period = request_period(body.value("period", period));
    const std::int64_t default_threshold = json_record::as_i64(config.record.value("unhealthy_after_seconds", 300), 300);
    auto result = repo.maintain_data_push_jobs_runtime(
        resolved_period,
        {
            {"period", resolved_period},
            {"unhealthy_after_seconds", json_record::as_i64(body.value("unhealthy_after_seconds", default_threshold), default_threshold)},
        },
        _now);
    if (result.record.is_object())
    {
        result.record["config"] = config.record;
        result.record["manual"] = true;
    }
    return repository_result(200, result);
}

ControllerResponse OperationsController::scheduled_data_push_maintenance(const std::string &period, std::int64_t last_run_time)
{
    storage::AccountDomainRepository repo(_redis);
    auto config = repo.get_data_push_maintenance_config(_now);
    if (config.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(config.status, config.error);
    }

    const std::string resolved_period = request_period(period);
    if (!config.record.value("enabled", true))
    {
        return json_response(200, {
            {"period", resolved_period},
            {"executed", false},
            {"skip_reason", "disabled"},
            {"config", config.record},
        });
    }

    const auto interval_seconds = json_record::as_i64(config.record.value("interval_seconds", 60), 60);
    if (last_run_time > 0 && _now - last_run_time < interval_seconds)
    {
        return json_response(200, {
            {"period", resolved_period},
            {"executed", false},
            {"skip_reason", "interval"},
            {"elapsed_seconds", _now - last_run_time},
            {"interval_seconds", interval_seconds},
            {"config", config.record},
        });
    }

    const auto unhealthy_after_seconds = json_record::as_i64(config.record.value("unhealthy_after_seconds", 300), 300);
    auto result = repo.maintain_data_push_jobs_runtime(
        resolved_period,
        {
            {"period", resolved_period},
            {"unhealthy_after_seconds", unhealthy_after_seconds},
        },
        _now);
    if (result.record.is_object())
    {
        result.record["config"] = config.record;
        result.record["executed"] = true;
    }
    return repository_result(200, result);
}

ControllerResponse OperationsController::maintain_data_push_jobs_runtime(const std::string &period, std::int64_t unhealthy_after_seconds)
{
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.maintain_data_push_jobs_runtime(
        request_period(period),
        {
            {"period", request_period(period)},
            {"unhealthy_after_seconds", unhealthy_after_seconds},
        },
        _now);
    return repository_result(200, result);
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
