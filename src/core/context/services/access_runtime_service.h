#pragma once

#include "json_record.h"

#include <cmath>
#include <cstdint>
#include <ctime>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::core
{

inline constexpr const char *ACCESS_RUNTIME_KIND_USER_CLIENT = "user_client";
inline constexpr const char *ACCESS_RUNTIME_KIND_SUPPLIER_STATION = "supplier_station";
inline constexpr const char *ACCESS_RUNTIME_STATUS_ACTIVE = "active";
inline constexpr const char *ACCESS_RUNTIME_BILLING_MODE_PAYG = "payg";
inline constexpr const char *ACCESS_RUNTIME_BILLING_MODE_SUBSCRIPTION = "subscription";

struct AccessRuntimeValidation
{
    bool ok = false;
    std::string reason;
};

struct AccessRuntimeRecordInput
{
    std::string owner_account_id;
    std::string access_account_id;
    std::string access_username;
    std::string access_kind;
    std::string mountpoint;
    std::string group_id;
    std::string connect_key;
    std::string auth_type;
    std::string addr;
    int port = 0;
    std::string user_agent;
    std::string ntrip_version;
    std::string billing_mode = ACCESS_RUNTIME_BILLING_MODE_PAYG;
    std::int64_t start_time = 0;
    std::int64_t update_time = 0;
    std::int64_t end_time = 0;
    std::int64_t used_seconds = 0;
    std::int64_t stat_cost_cents = 0;
    std::int64_t actual_debit_cents = 0;
    std::int64_t balance_after_cents = 0;
    std::string disconnect_reason;
};

struct AccessRuntimeSessionRevalidationInput
{
    nlohmann::json active_record;
    std::string auth_type;
    std::string mountpoint;
    std::int64_t now = 0;
    std::int64_t next_slice_seconds = 60;
    std::int64_t hourly_price_cents = 0;
    double billing_multiplier = 1.0;
};

inline bool is_access_runtime_auth_index(const nlohmann::json &record)
{
    return record.is_object() &&
           !json_record::string_field(record, "access_account_id").empty() &&
           !json_record::string_field(record, "owner_account_id").empty();
}

inline bool is_runtime_status_active(const nlohmann::json &record, const char *field)
{
    return json_record::string_field(record, field) == ACCESS_RUNTIME_STATUS_ACTIVE;
}

inline bool is_runtime_kind_allowed_for_auth_type(const std::string &kind, const std::string &auth_type)
{
    if (auth_type == "client")
    {
        return kind == ACCESS_RUNTIME_KIND_USER_CLIENT;
    }
    if (auth_type == "server" || auth_type == "source")
    {
        return kind == ACCESS_RUNTIME_KIND_SUPPLIER_STATION;
    }
    return false;
}

inline AccessRuntimeValidation validate_access_auth_index(const nlohmann::json &record,
                                                          const std::string &auth_type,
                                                          const std::string &mountpoint,
                                                          std::int64_t now,
                                                          bool require_mountpoint)
{
    if (!is_access_runtime_auth_index(record))
    {
        return {false, "access_runtime_index_invalid"};
    }
    if (!is_runtime_status_active(record, "access_status"))
    {
        return {false, "access_account_disabled"};
    }
    if (!is_runtime_status_active(record, "owner_status"))
    {
        return {false, "owner_account_disabled"};
    }
    const std::string kind = json_record::string_field(record, "access_kind");
    if (!is_runtime_kind_allowed_for_auth_type(kind, auth_type))
    {
        return {false, "access_kind_not_allowed"};
    }
    if (require_mountpoint && mountpoint.empty())
    {
        return {false, "mountpoint_required"};
    }
    if (json_record::string_field(record, "mount_point_group_id").empty())
    {
        return {false, "mount_point_group_required"};
    }
    const auto expire_it = record.find("expire_time");
    const std::int64_t expire_time = expire_it == record.end() ? 0 : json_record::as_i64(*expire_it);
    if (expire_time > 0 && now > expire_time)
    {
        return {false, "access_account_expired"};
    }
    const std::int64_t balance = json_record::as_i64(record.value("balance_cents", 0), 0);
    const std::int64_t credit = json_record::as_i64(record.value("credit_limit_cents", 0), 0);
    if (kind == ACCESS_RUNTIME_KIND_USER_CLIENT && balance + credit < 0)
    {
        return {false, "balance_insufficient"};
    }
    return {true, ""};
}

inline AccessRuntimeValidation validate_access_dependencies(const nlohmann::json &grant,
                                                            const nlohmann::json &group,
                                                            const nlohmann::json &member,
                                                            const nlohmann::json &mount_record)
{
    if (!grant.is_object() || !is_runtime_status_active(grant, "status"))
    {
        return {false, "group_grant_revoked"};
    }
    if (!group.is_object() || !is_runtime_status_active(group, "status"))
    {
        return {false, "mount_point_group_disabled"};
    }
    if (!member.is_object() || !is_runtime_status_active(member, "status"))
    {
        return {false, "mountpoint_not_in_group"};
    }
    if (mount_record.is_object() && mount_record.contains("status") && !is_runtime_status_active(mount_record, "status"))
    {
        return {false, "mountpoint_disabled"};
    }
    return {true, ""};
}

inline std::string runtime_period_from_unix(std::int64_t ts)
{
    std::time_t time_value = static_cast<std::time_t>(ts);
    std::tm tm_value{};
#if defined(_WIN32)
    gmtime_s(&tm_value, &time_value);
#else
    gmtime_r(&time_value, &tm_value);
#endif
    char buffer[7] = {};
    std::strftime(buffer, sizeof(buffer), "%Y%m", &tm_value);
    return buffer;
}

inline std::int64_t calculate_runtime_cost_cents(std::int64_t used_seconds,
                                                 std::int64_t hourly_price_cents,
                                                 double billing_multiplier)
{
    if (used_seconds <= 0 || hourly_price_cents <= 0 || billing_multiplier <= 0.0)
    {
        return 0;
    }
    const double cost = (static_cast<double>(used_seconds) / 3600.0) *
                        static_cast<double>(hourly_price_cents) *
                        billing_multiplier;
    return static_cast<std::int64_t>(std::llround(cost));
}

inline AccessRuntimeValidation revalidate_access_runtime_session(const AccessRuntimeSessionRevalidationInput &input)
{
    const bool require_mountpoint = input.auth_type != "source";
    auto validation = validate_access_auth_index(
        input.active_record,
        input.auth_type,
        input.mountpoint,
        input.now,
        require_mountpoint);
    if (!validation.ok)
    {
        return validation;
    }

    const std::string kind = json_record::string_field(input.active_record, "access_kind");
    if (kind == ACCESS_RUNTIME_KIND_USER_CLIENT)
    {
        const std::int64_t balance = json_record::as_i64(input.active_record.value("balance_cents", 0), 0);
        const std::int64_t credit = json_record::as_i64(input.active_record.value("credit_limit_cents", 0), 0);
        const auto next_slice_cost = calculate_runtime_cost_cents(
            input.next_slice_seconds,
            input.hourly_price_cents,
            input.billing_multiplier);
        if (next_slice_cost > 0 && next_slice_cost > balance + credit)
        {
            return {false, "balance_insufficient"};
        }
    }
    return {true, ""};
}

inline std::string runtime_billing_id(const AccessRuntimeRecordInput &input)
{
    return input.connect_key + ":" + std::to_string(input.start_time) + ":" + std::to_string(input.end_time);
}

inline std::string runtime_billing_fingerprint(const AccessRuntimeRecordInput &input)
{
    return input.owner_account_id + "|" + input.access_account_id + "|" + input.mountpoint + "|" +
           input.group_id + "|" + std::to_string(input.used_seconds) + "|" +
           input.billing_mode + "|" + std::to_string(input.stat_cost_cents);
}

inline nlohmann::json build_online_session_record(const AccessRuntimeRecordInput &input)
{
    return {
        {"connect_key", input.connect_key},
        {"account_id", input.owner_account_id},
        {"owner_account_id", input.owner_account_id},
        {"access_account_id", input.access_account_id},
        {"access_username", input.access_username},
        {"kind", input.access_kind},
        {"mountpoint", input.mountpoint},
        {"group_id", input.group_id},
        {"billing_mode", input.billing_mode},
        {"auth_type", input.auth_type},
        {"start_time", input.start_time},
        {"update_time", input.update_time},
        {"addr", input.addr},
        {"port", input.port},
        {"user_agent", input.user_agent},
        {"ntrip_version", input.ntrip_version},
    };
}

inline nlohmann::json build_billing_usage_entry(const AccessRuntimeRecordInput &input)
{
    const std::string billing_id = runtime_billing_id(input);
    return {
        {"billing_id", billing_id},
        {"fingerprint", runtime_billing_fingerprint(input)},
        {"account_id", input.owner_account_id},
        {"access_account_id", input.access_account_id},
        {"access_username", input.access_username},
        {"mountpoint", input.mountpoint},
        {"group_id", input.group_id},
        {"connect_key", input.connect_key},
        {"start_time", input.start_time},
        {"end_time", input.end_time},
        {"used_seconds", input.used_seconds},
        {"billing_mode", input.billing_mode},
        {"stat_cost_cents", input.stat_cost_cents},
        {"actual_debit_cents", input.actual_debit_cents},
        {"disconnect_reason", input.disconnect_reason},
    };
}

inline nlohmann::json build_balance_ledger_entry(const AccessRuntimeRecordInput &input)
{
    const std::string billing_id = runtime_billing_id(input);
    return {
        {"ledger_id", "ledger:" + billing_id},
        {"account_id", input.owner_account_id},
        {"delta_cents", -input.actual_debit_cents},
        {"balance_after_cents", input.balance_after_cents},
        {"source", "billing_usage"},
        {"billing_id", billing_id},
        {"access_account_id", input.access_account_id},
        {"connect_key", input.connect_key},
    };
}

inline nlohmann::json build_supplier_supply_usage(const AccessRuntimeRecordInput &input)
{
    const std::string billing_id = runtime_billing_id(input);
    return {
        {"usage_id", "supply:" + billing_id},
        {"supplier_account_id", input.owner_account_id},
        {"access_account_id", input.access_account_id},
        {"station_id", "st_" + input.mountpoint},
        {"mountpoint", input.mountpoint},
        {"session_id", input.connect_key},
        {"start_time", input.start_time},
        {"end_time", input.end_time},
        {"used_seconds", input.used_seconds},
        {"earning_rule_snapshot", "fixed_hourly_rate:0"},
        {"earning_cents", 0},
        {"status", "pending"},
    };
}

inline nlohmann::json build_station_record(const AccessRuntimeRecordInput &input, bool current_online)
{
    return {
        {"station_id", "st_" + input.mountpoint},
        {"mountpoint", input.mountpoint},
        {"display_name", input.mountpoint},
        {"first_seen_time", input.start_time},
        {"last_seen_time", current_online ? input.update_time : input.end_time},
        {"total_online_seconds", current_online ? 0 : input.used_seconds},
        {"current_online", current_online},
        {"last_access_account_id", input.access_account_id},
        {"last_supplier_account_id", input.owner_account_id},
        {"last_connect_key", input.connect_key},
    };
}

inline nlohmann::json build_station_event(const AccessRuntimeRecordInput &input, const std::string &event_type)
{
    const std::int64_t event_time = event_type == "login" ? input.start_time : input.end_time;
    return {
        {"event_id", "station:" + event_type + ":" + input.connect_key + ":" + std::to_string(event_time)},
        {"mountpoint", input.mountpoint},
        {"event_type", event_type},
        {"event_time", event_time},
        {"supplier_account_id", input.owner_account_id},
        {"access_account_id", input.access_account_id},
        {"session_id", input.connect_key},
        {"disconnect_reason", input.disconnect_reason},
    };
}

} // namespace navcaster::core
