#include "account_domain.h"

#include "json_record.h"

#include <utility>

namespace navcaster::account_domain
{
namespace
{
bool fail(std::string *error, const std::string &message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}

std::string string_value(const nlohmann::json &record, const char *field)
{
    return json_record::string_field(record, field);
}

bool require_string(const nlohmann::json &record, const char *field, std::string *error)
{
    if (!has_nonempty_string(record, field))
    {
        return fail(error, std::string(field) + " is required");
    }
    return true;
}

void touch(nlohmann::json &record, std::int64_t now)
{
    json_record::touch_timestamps(record, now);
    record["schema_version"] = record.value("schema_version", CURRENT_SCHEMA_VERSION);
}

void default_status(nlohmann::json &record)
{
    record["status"] = record.value("status", std::string(STATUS_ACTIVE));
}

bool validate_status(const nlohmann::json &record, std::string *error)
{
    const std::string status = string_value(record, "status");
    if (!is_resource_status(status))
    {
        return fail(error, "invalid status");
    }
    return true;
}

bool normalize_append_fact(nlohmann::json &record,
                           const char *id_field,
                           std::int64_t now,
                           std::string *error)
{
    if (!require_string(record, id_field, error))
    {
        return false;
    }
    record["create_time"] = record.value("create_time", now);
    record["schema_version"] = record.value("schema_version", CURRENT_SCHEMA_VERSION);
    return true;
}
} // namespace

bool is_account_role(const std::string &role)
{
    return role == ROLE_ADMIN || role == ROLE_USER || role == ROLE_SUPPLIER;
}

bool is_resource_status(const std::string &status)
{
    return status == STATUS_ACTIVE || status == STATUS_DISABLED || status == STATUS_DELETED;
}

bool is_active_status(const nlohmann::json &record)
{
    return string_value(record, "status") == STATUS_ACTIVE;
}

bool is_access_account_kind(const std::string &kind)
{
    return kind == ACCESS_KIND_USER_CLIENT || kind == ACCESS_KIND_SUPPLIER_STATION;
}

bool is_access_kind_allowed_for_role(const std::string &role, const std::string &kind)
{
    if (role == ROLE_ADMIN)
    {
        return is_access_account_kind(kind);
    }
    if (role == ROLE_USER)
    {
        return kind == ACCESS_KIND_USER_CLIENT;
    }
    if (role == ROLE_SUPPLIER)
    {
        return kind == ACCESS_KIND_SUPPLIER_STATION;
    }
    return false;
}

bool is_nonnegative_number(const nlohmann::json &value)
{
    if (value.is_number_unsigned())
    {
        return true;
    }
    if (value.is_number_integer())
    {
        return value.get<std::int64_t>() >= 0;
    }
    if (value.is_number_float())
    {
        return value.get<double>() >= 0.0;
    }
    return false;
}

bool has_nonempty_string(const nlohmann::json &record, const char *field)
{
    return !json_record::string_field(record, field).empty();
}

bool normalize_account_record(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "account_id", error) || !require_string(record, "username", error))
    {
        return false;
    }
    const std::string role = string_value(record, "role");
    if (!is_account_role(role))
    {
        return fail(error, "invalid account role");
    }
    default_status(record);
    if (!validate_status(record, error))
    {
        return false;
    }
    record["balance_cents"] = record.value("balance_cents", 0);
    record["credit_limit_cents"] = record.value("credit_limit_cents", 0);
    record["concurrency_limit"] = record.value("concurrency_limit", 1);
    if (!is_nonnegative_number(record["balance_cents"]) ||
        !is_nonnegative_number(record["credit_limit_cents"]) ||
        !is_nonnegative_number(record["concurrency_limit"]))
    {
        return fail(error, "account numeric fields must be non-negative");
    }
    touch(record, now);
    return true;
}

bool normalize_mount_point_group(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "group_id", error) || !require_string(record, "name", error))
    {
        return false;
    }
    default_status(record);
    if (!validate_status(record, error))
    {
        return false;
    }
    record["billing_multiplier"] = record.value("billing_multiplier", 1.0);
    if (!is_nonnegative_number(record["billing_multiplier"]))
    {
        return fail(error, "billing_multiplier must be non-negative");
    }
    touch(record, now);
    return true;
}

bool normalize_mount_point(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "mountpoint", error))
    {
        return false;
    }
    default_status(record);
    if (!validate_status(record, error))
    {
        return false;
    }
    record["hourly_price_cents"] = record.value("hourly_price_cents", 0);
    if (!is_nonnegative_number(record["hourly_price_cents"]))
    {
        return fail(error, "hourly_price_cents must be non-negative");
    }
    touch(record, now);
    return true;
}

bool normalize_access_account(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "access_account_id", error) ||
        !require_string(record, "owner_account_id", error) ||
        !require_string(record, "username", error) ||
        !require_string(record, "mount_point_group_id", error))
    {
        return false;
    }
    const std::string kind = string_value(record, "kind");
    if (!is_access_account_kind(kind))
    {
        return fail(error, "invalid access account kind");
    }
    default_status(record);
    if (!validate_status(record, error))
    {
        return false;
    }
    record["concurrency_limit"] = record.value("concurrency_limit", 1);
    record["expire_time"] = record.value("expire_time", 0);
    if (!is_nonnegative_number(record["concurrency_limit"]) || !is_nonnegative_number(record["expire_time"]))
    {
        return fail(error, "access account numeric fields must be non-negative");
    }
    touch(record, now);
    return true;
}

bool normalize_subscription(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "subscription_id", error) || !require_string(record, "account_id", error))
    {
        return false;
    }
    if (!record.contains("group_ids") || !record["group_ids"].is_array() || record["group_ids"].empty())
    {
        return fail(error, "group_ids is required");
    }
    default_status(record);
    if (!validate_status(record, error))
    {
        return false;
    }
    record["start_time"] = record.value("start_time", now);
    record["expire_time"] = record.value("expire_time", 0);
    touch(record, now);
    return true;
}

bool normalize_station_record(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "mountpoint", error))
    {
        return false;
    }
    record["station_id"] = record.value("station_id", std::string("st_") + string_value(record, "mountpoint"));
    record["first_seen_time"] = record.value("first_seen_time", now);
    record["last_seen_time"] = record.value("last_seen_time", now);
    record["total_online_seconds"] = record.value("total_online_seconds", 0);
    record["current_online"] = record.value("current_online", false);
    touch(record, now);
    return true;
}

bool normalize_station_event(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!normalize_append_fact(record, "event_id", now, error) ||
        !require_string(record, "mountpoint", error) ||
        !require_string(record, "event_type", error))
    {
        return false;
    }
    record["event_time"] = record.value("event_time", now);
    return true;
}

bool normalize_balance_ledger_entry(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!normalize_append_fact(record, "ledger_id", now, error) || !require_string(record, "account_id", error))
    {
        return false;
    }
    record["delta_cents"] = record.value("delta_cents", 0);
    record["balance_after_cents"] = record.value("balance_after_cents", 0);
    return true;
}

bool normalize_billing_usage_entry(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!normalize_append_fact(record, "billing_id", now, error) ||
        !require_string(record, "account_id", error) ||
        !require_string(record, "access_account_id", error) ||
        !require_string(record, "mountpoint", error))
    {
        return false;
    }
    if (!has_nonempty_string(record, "fingerprint"))
    {
        return fail(error, "fingerprint is required");
    }
    record["used_seconds"] = record.value("used_seconds", 0);
    record["stat_cost_cents"] = record.value("stat_cost_cents", 0);
    record["actual_debit_cents"] = record.value("actual_debit_cents", 0);
    return true;
}

bool normalize_data_push_usage(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!normalize_append_fact(record, "usage_id", now, error) ||
        !require_string(record, "account_id", error) ||
        !require_string(record, "target_mountpoint", error))
    {
        return false;
    }
    record["used_seconds"] = record.value("used_seconds", 0);
    record["stat_cost_cents"] = record.value("stat_cost_cents", 0);
    record["actual_debit_cents"] = record.value("actual_debit_cents", 0);
    return true;
}

bool normalize_supplier_supply_usage(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!normalize_append_fact(record, "usage_id", now, error) ||
        !require_string(record, "supplier_account_id", error) ||
        !require_string(record, "access_account_id", error) ||
        !require_string(record, "mountpoint", error))
    {
        return false;
    }
    record["used_seconds"] = record.value("used_seconds", 0);
    record["earning_cents"] = record.value("earning_cents", 0);
    record["status"] = record.value("status", std::string("pending"));
    return true;
}

nlohmann::json username_index_record(const std::string &id, const std::string &status, std::int64_t now)
{
    return {
        {"id", id},
        {"status", status},
        {"update_time", now},
    };
}

nlohmann::json access_account_owner_summary(const nlohmann::json &record)
{
    return {
        {"access_account_id", record.value("access_account_id", std::string{})},
        {"username", record.value("username", std::string{})},
        {"kind", record.value("kind", std::string{})},
        {"status", record.value("status", std::string{})},
        {"mount_point_group_id", record.value("mount_point_group_id", std::string{})},
        {"update_time", record.value("update_time", 0LL)},
    };
}

nlohmann::json access_account_auth_index(const nlohmann::json &record, const nlohmann::json &owner)
{
    return {
        {"access_account_id", record.value("access_account_id", std::string{})},
        {"access_username", record.value("username", std::string{})},
        {"access_kind", record.value("kind", std::string{})},
        {"access_status", record.value("status", std::string{})},
        {"owner_account_id", record.value("owner_account_id", std::string{})},
        {"owner_role", owner.value("role", std::string{})},
        {"owner_status", owner.value("status", std::string{})},
        {"mount_point_group_id", record.value("mount_point_group_id", std::string{})},
        {"account_concurrency_limit", owner.value("concurrency_limit", 0)},
        {"access_concurrency_limit", record.value("concurrency_limit", 0)},
        {"expire_time", record.value("expire_time", 0LL)},
    };
}

} // namespace navcaster::account_domain
