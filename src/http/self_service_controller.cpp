#include "self_service_controller.h"

#include "account_domain.h"
#include "account_domain_repository.h"
#include "controller_helpers.h"
#include "json_record.h"
#include "redis_keys.h"

#include <algorithm>
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
    static constexpr const char *fields[] = {
        "password",
        "old_password",
        "password_hash",
        "password_algo",
        "password_salt",
        "password_iterations",
    };
    for (const auto *field : fields)
    {
        record.erase(field);
    }
    return record;
}

bool parse_body_object(const std::string &body_text, nlohmann::json &body)
{
    return parse_json_body(body_text, body) && body.is_object();
}

std::string request_period(const std::string &period)
{
    return period.empty() ? "current" : period;
}

bool is_scope(const std::string &scope, const char *expected)
{
    return scope == expected;
}

bool contains_string(const nlohmann::json &items, const std::string &value)
{
    if (!items.is_array())
    {
        return false;
    }
    for (const auto &item : items)
    {
        if (item.is_string() && item.get<std::string>() == value)
        {
            return true;
        }
    }
    return false;
}
} // namespace

SelfServiceController::SelfServiceController(storage::RedisHashClient &redis, std::int64_t now)
    : _redis(redis), _now(now)
{
}

ControllerResponse SelfServiceController::session_subject(const AuthSessionSubject &subject)
{
    if (!subject.authenticated)
    {
        return error_response(401, "Unauthorized");
    }
    nlohmann::json body = {
        {"username", subject.username},
        {"account_id", subject.account_id},
        {"role", subject.role},
        {"status", subject.status},
        {"compat_admin", subject.compat_admin},
    };
    const auto account = owner_account(subject);
    if (account.is_object())
    {
        body["account"] = sanitized_record(account);
    }
    return json_response(200, body);
}

bool SelfServiceController::scope_allowed(const AuthSessionSubject &subject, const std::string &scope) const
{
    if (!subject.authenticated)
    {
        return false;
    }
    if (subject.role == navcaster::account_domain::ROLE_ADMIN)
    {
        return true;
    }
    if (is_scope(scope, "me"))
    {
        return subject.role == navcaster::account_domain::ROLE_USER;
    }
    if (is_scope(scope, "supplier"))
    {
        return subject.role == navcaster::account_domain::ROLE_SUPPLIER;
    }
    return false;
}

ControllerResponse SelfServiceController::subject_error(const AuthSessionSubject &subject, const std::string &scope) const
{
    if (!subject.authenticated)
    {
        return error_response(401, "Unauthorized");
    }
    if (!scope_allowed(subject, scope))
    {
        return error_response(403, "Forbidden");
    }
    if (subject.account_id.empty())
    {
        return error_response(404, "Account subject not found");
    }
    const auto account = owner_account(subject);
    if (!account.is_object() || !navcaster::account_domain::is_active_status(account))
    {
        return error_response(404, "Account subject not found");
    }
    ControllerResponse ok;
    ok.status_code = 0;
    return ok;
}

std::string SelfServiceController::expected_kind(const AuthSessionSubject &subject, const std::string &scope) const
{
    if (subject.role == navcaster::account_domain::ROLE_ADMIN)
    {
        return is_scope(scope, "supplier")
            ? navcaster::account_domain::ACCESS_KIND_SUPPLIER_STATION
            : navcaster::account_domain::ACCESS_KIND_USER_CLIENT;
    }
    return subject.role == navcaster::account_domain::ROLE_SUPPLIER
        ? navcaster::account_domain::ACCESS_KIND_SUPPLIER_STATION
        : navcaster::account_domain::ACCESS_KIND_USER_CLIENT;
}

nlohmann::json SelfServiceController::owner_account(const AuthSessionSubject &subject) const
{
    if (subject.account_id.empty())
    {
        return nullptr;
    }
    return _redis.hget(redis_keys::ACC_RECORD, subject.account_id.c_str());
}

bool SelfServiceController::owns_access_account(const AuthSessionSubject &subject, const nlohmann::json &record) const
{
    return record.is_object() && record.value("owner_account_id", std::string{}) == subject.account_id;
}

ControllerResponse SelfServiceController::repository_result(int success_status, const storage::AccountDomainResult &result) const
{
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(success_status, sanitized_record(result.record));
}

nlohmann::json SelfServiceController::owner_group_grants(const AuthSessionSubject &subject) const
{
    nlohmann::json records = nlohmann::json::object();
    const auto grants = _redis.hgetall(redis_keys::acc_group(subject.account_id).c_str());
    if (!grants.is_object())
    {
        return records;
    }
    for (auto it = grants.begin(); it != grants.end(); ++it)
    {
        if (!it.value().is_object() || !navcaster::account_domain::is_active_status(it.value()))
        {
            continue;
        }
        auto grant = it.value();
        const std::string group_id = grant.value("group_id", it.key());
        const auto group = _redis.hget(redis_keys::MPGRP_RECORD, group_id.c_str());
        if (group.is_object() && navcaster::account_domain::is_active_status(group))
        {
            grant["group"] = group;
        }
        records[group_id] = grant;
    }
    return records;
}

nlohmann::json SelfServiceController::visible_mount_points(const AuthSessionSubject &subject) const
{
    const auto grants = owner_group_grants(subject);
    nlohmann::json records = nlohmann::json::object();
    for (auto it = grants.begin(); it != grants.end(); ++it)
    {
        const std::string group_id = it.key();
        const auto members = _redis.hgetall(redis_keys::mpgrp_member(group_id).c_str());
        if (!members.is_object())
        {
            continue;
        }
        for (auto member = members.begin(); member != members.end(); ++member)
        {
            const std::string mountpoint = member.value().value("mountpoint", member.key());
            if (mountpoint.empty())
            {
                continue;
            }
            auto record = _redis.hget(redis_keys::MOUNT_RECORD, mountpoint.c_str());
            if (!record.is_object())
            {
                record = nlohmann::json{{"mountpoint", mountpoint}};
            }
            record["group_id"] = group_id;
            records[mountpoint] = record;
        }
    }
    return records;
}

nlohmann::json SelfServiceController::owner_access_accounts(const AuthSessionSubject &subject, const std::string &scope) const
{
    const auto all = _redis.hgetall(redis_keys::AACC_RECORD);
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return records;
    }
    const std::string kind = expected_kind(subject, scope);
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        const auto &record = it.value();
        if (owns_access_account(subject, record) &&
            record.value("kind", std::string{}) == kind &&
            record.value("status", std::string{}) != navcaster::account_domain::STATUS_DELETED)
        {
            records[it.key()] = sanitized_record(record);
        }
    }
    return records;
}

ControllerResponse SelfServiceController::profile(const AuthSessionSubject &subject, const std::string &scope)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, sanitized_record(owner_account(subject)));
}

ControllerResponse SelfServiceController::dashboard(const AuthSessionSubject &subject, const std::string &scope)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    const auto account = owner_account(subject);
    const auto access_accounts = owner_access_accounts(subject, scope);
    const auto grants = owner_group_grants(subject);
    nlohmann::json body = {
        {"account", sanitized_record(account)},
        {"access_account_count", access_accounts.size()},
        {"allowed_group_count", grants.size()},
        {"balance_cents", account.value("balance_cents", 0)},
        {"concurrency_limit", account.value("concurrency_limit", 0)},
    };
    if (is_scope(scope, "supplier"))
    {
        body["supply_usage_count"] = filter_supply_usage(subject.account_id, "current").size();
    }
    else
    {
        body["usage_count"] = filter_billing_usage(subject.account_id, "current").size();
    }
    return json_response(200, body);
}

ControllerResponse SelfServiceController::allowed_groups(const AuthSessionSubject &subject)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, owner_group_grants(subject));
}

ControllerResponse SelfServiceController::mount_points(const AuthSessionSubject &subject)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, visible_mount_points(subject));
}

ControllerResponse SelfServiceController::list_access_accounts(const AuthSessionSubject &subject, const std::string &scope)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, owner_access_accounts(subject, scope));
}

ControllerResponse SelfServiceController::get_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    storage::AccountDomainRepository repo(_redis);
    const auto result = repo.get_access_account(access_account_id);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    if (!owns_access_account(subject, result.record) ||
        result.record.value("kind", std::string{}) != expected_kind(subject, scope))
    {
        return error_response(404, "AccessAccount not found");
    }
    return json_response(200, sanitized_record(result.record));
}

ControllerResponse SelfServiceController::create_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &body_text)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["owner_account_id"] = subject.account_id;
    body["kind"] = expected_kind(subject, scope);
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_access_account(std::move(body), _now);
    return repository_result(201, result);
}

ControllerResponse SelfServiceController::update_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id, const std::string &body_text)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    storage::AccountDomainRepository repo(_redis);
    const auto current = repo.get_access_account(access_account_id);
    if (current.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(current.status, current.error);
    }
    if (!owns_access_account(subject, current.record) ||
        current.record.value("kind", std::string{}) != expected_kind(subject, scope))
    {
        return error_response(404, "AccessAccount not found");
    }
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["owner_account_id"] = subject.account_id;
    body["kind"] = expected_kind(subject, scope);
    auto result = repo.update_access_account(access_account_id, std::move(body), _now);
    return repository_result(200, result);
}

ControllerResponse SelfServiceController::update_access_account_password(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id, const std::string &body_text)
{
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    const std::string password = body.value("password", std::string{});
    if (password.empty())
    {
        return error_response(400, "password is required");
    }
    nlohmann::json update = {
        {"password", password},
    };
    if (body.contains("password_salt"))
    {
        update["password_salt"] = body["password_salt"];
    }
    if (body.contains("password_iterations"))
    {
        update["password_iterations"] = body["password_iterations"];
    }
    return update_access_account(subject, scope, access_account_id, update.dump());
}

ControllerResponse SelfServiceController::delete_access_account(const AuthSessionSubject &subject, const std::string &scope, const std::string &access_account_id)
{
    auto guard = subject_error(subject, scope);
    if (guard.status_code != 0)
    {
        return guard;
    }
    storage::AccountDomainRepository repo(_redis);
    const auto current = repo.get_access_account(access_account_id);
    if (current.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(current.status, current.error);
    }
    if (!owns_access_account(subject, current.record) ||
        current.record.value("kind", std::string{}) != expected_kind(subject, scope))
    {
        return error_response(404, "AccessAccount not found");
    }
    return repository_result(200, repo.delete_access_account(access_account_id, _now));
}

nlohmann::json SelfServiceController::filter_billing_usage(const std::string &account_id, const std::string &period) const
{
    const auto all = _redis.hgetall(redis_keys::bill_entry(request_period(period)).c_str());
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return records;
    }
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().is_object() && it.value().value("account_id", std::string{}) == account_id)
        {
            records[it.key()] = it.value();
        }
    }
    return records;
}

nlohmann::json SelfServiceController::filter_data_push_usage(const std::string &account_id, const std::string &period) const
{
    const auto all = _redis.hgetall(redis_keys::data_push(request_period(period)).c_str());
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return records;
    }
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().is_object() && it.value().value("account_id", std::string{}) == account_id)
        {
            records[it.key()] = it.value();
        }
    }
    return records;
}

nlohmann::json SelfServiceController::active_data_push_configs() const
{
    const auto all = _redis.hgetall(redis_keys::DATA_PUSH_CONFIG);
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return records;
    }
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().is_object() && navcaster::account_domain::is_active_status(it.value()))
        {
            records[it.key()] = sanitized_record(it.value());
        }
    }
    return records;
}

nlohmann::json SelfServiceController::filter_data_push_jobs(const std::string &account_id, const std::string &period) const
{
    const auto all = _redis.hgetall(redis_keys::data_push_job(request_period(period)).c_str());
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return records;
    }
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().is_object() && it.value().value("account_id", std::string{}) == account_id)
        {
            records[it.key()] = sanitized_record(it.value());
        }
    }
    return records;
}

ControllerResponse SelfServiceController::usage(const AuthSessionSubject &subject, const std::string &period)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, filter_billing_usage(subject.account_id, period));
}

nlohmann::json SelfServiceController::owner_subscriptions(const std::string &account_id) const
{
    const auto all = _redis.hgetall(redis_keys::sub_account(account_id).c_str());
    return all.is_object() ? all : nlohmann::json::object();
}

nlohmann::json SelfServiceController::owner_redeem_redemptions(const std::string &account_id) const
{
    const auto all = _redis.hgetall(redis_keys::redeem_account(account_id).c_str());
    return all.is_object() ? all : nlohmann::json::object();
}

ControllerResponse SelfServiceController::subscriptions(const AuthSessionSubject &subject)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, owner_subscriptions(subject.account_id));
}

ControllerResponse SelfServiceController::redeem_redemptions(const AuthSessionSubject &subject)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, owner_redeem_redemptions(subject.account_id));
}

ControllerResponse SelfServiceController::data_push_configs(const AuthSessionSubject &subject)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, active_data_push_configs());
}

ControllerResponse SelfServiceController::data_push_jobs(const AuthSessionSubject &subject, const std::string &period)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, filter_data_push_jobs(subject.account_id, period));
}

ControllerResponse SelfServiceController::create_data_push_job(const AuthSessionSubject &subject, const std::string &body_text)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    nlohmann::json body;
    if (!parse_body_object(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }
    body["account_id"] = subject.account_id;
    body.erase("usage_id");
    body.erase("ledger_id");
    body.erase("balance_after_cents");
    body.erase("stat_cost_cents");
    body.erase("actual_debit_cents");
    const std::string period = request_period(body.value("period", std::string{}));
    body["period"] = period;
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.create_data_push_job(std::move(body), period, _now);
    return repository_result(201, result);
}

ControllerResponse SelfServiceController::data_push_usage(const AuthSessionSubject &subject, const std::string &period)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, filter_data_push_usage(subject.account_id, period));
}

ControllerResponse SelfServiceController::append_data_push_usage(const AuthSessionSubject &subject, const std::string &body_text)
{
    auto guard = subject_error(subject, "me");
    if (guard.status_code != 0)
    {
        return guard;
    }
    nlohmann::json body;
    if (!parse_json_body(body_text, body) || !body.is_object())
    {
        return error_response(400, "Invalid JSON body");
    }
    body["account_id"] = subject.account_id;
    body.erase("ledger_id");
    body.erase("balance_after_cents");
    const std::string period = request_period(body.value("period", std::string{}));
    if (!body.contains("usage_id"))
    {
        return error_response(400, "usage_id is required");
    }
    if (!body.contains("stat_cost_cents") && body.contains("actual_debit_cents"))
    {
        body["stat_cost_cents"] = body["actual_debit_cents"];
    }
    if (!body.contains("actual_debit_cents") && body.contains("stat_cost_cents"))
    {
        body["actual_debit_cents"] = body["stat_cost_cents"];
    }
    storage::AccountDomainRepository repo(_redis);
    auto result = repo.append_data_push_usage_with_balance(std::move(body), period, _now);
    return repository_result(201, result);
}

ControllerResponse SelfServiceController::supplier_stations(const AuthSessionSubject &subject)
{
    auto guard = subject_error(subject, "supplier");
    if (guard.status_code != 0)
    {
        return guard;
    }
    const auto all = _redis.hgetall(redis_keys::STATION_RECORD);
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return json_response(200, records);
    }
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().is_object() &&
            it.value().value("last_supplier_account_id", std::string{}) == subject.account_id)
        {
            records[it.key()] = it.value();
        }
    }
    return json_response(200, records);
}

nlohmann::json SelfServiceController::filter_supply_usage(const std::string &account_id, const std::string &period) const
{
    const auto all = _redis.hgetall(redis_keys::supply_usage(request_period(period)).c_str());
    nlohmann::json records = nlohmann::json::object();
    if (!all.is_object())
    {
        return records;
    }
    for (auto it = all.begin(); it != all.end(); ++it)
    {
        if (it.value().is_object() && it.value().value("supplier_account_id", std::string{}) == account_id)
        {
            records[it.key()] = it.value();
        }
    }
    return records;
}

nlohmann::json SelfServiceController::supplier_settlement_records(const std::string &account_id, const std::string &period) const
{
    const auto records = _redis.hgetall(redis_keys::supply_earning(account_id, request_period(period)).c_str());
    return records.is_object() ? records : nlohmann::json::object();
}

ControllerResponse SelfServiceController::supplier_supply_usage(const AuthSessionSubject &subject, const std::string &period)
{
    auto guard = subject_error(subject, "supplier");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, filter_supply_usage(subject.account_id, period));
}

ControllerResponse SelfServiceController::supplier_settlements(const AuthSessionSubject &subject, const std::string &period)
{
    auto guard = subject_error(subject, "supplier");
    if (guard.status_code != 0)
    {
        return guard;
    }
    return json_response(200, supplier_settlement_records(subject.account_id, period));
}

ControllerResponse SelfServiceController::supplier_earnings(const AuthSessionSubject &subject, const std::string &period)
{
    auto guard = subject_error(subject, "supplier");
    if (guard.status_code != 0)
    {
        return guard;
    }
    const auto usage = filter_supply_usage(subject.account_id, period);
    long long pending_cents = 0;
    long long settled_cents = 0;
    long long total_seconds = 0;
    for (const auto &entry : usage)
    {
        if (!entry.is_object())
        {
            continue;
        }
        const long long earning = entry.value("earning_cents", 0LL);
        total_seconds += entry.value("used_seconds", 0LL);
        if (entry.value("status", std::string("pending")) == "settled")
        {
            settled_cents += earning;
        }
        else
        {
            pending_cents += earning;
        }
    }
    const auto settlements = supplier_settlement_records(subject.account_id, period);
    return json_response(200, {
        {"account_id", subject.account_id},
        {"period", request_period(period)},
        {"total_supply_seconds", total_seconds},
        {"pending_earning_cents", pending_cents},
        {"settled_earning_cents", settled_cents},
        {"total_earning_cents", pending_cents + settled_cents},
        {"settlement_count", settlements.is_object() ? static_cast<int>(settlements.size()) : 0},
    });
}

} // namespace navcaster::http_api
