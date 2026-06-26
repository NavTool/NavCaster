#include "account_domain.h"

#include "account_schema.h"
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
    normalize_password_material(record);
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
    normalize_password_material(record);
    touch(record, now);
    return true;
}

void normalize_password_material(nlohmann::json &record)
{
    if (!json_record::has_nonempty_string_field(record, "password"))
    {
        if (json_record::has_nonempty_string_field(record, "password_hash"))
        {
            record.erase("password");
        }
        else
        {
            record.erase("password");
        }
        return;
    }

    const std::string password = record.value("password", std::string{});
    const std::string salt = record.value("password_salt", std::string("access-account-salt"));
    const int iterations = record.value("password_iterations", account_schema::DEFAULT_PASSWORD_ITERATIONS);
    const int normalized_iterations = iterations <= 0 ? account_schema::DEFAULT_PASSWORD_ITERATIONS : iterations;
    record["password_hash"] = account_schema::make_password_hash(password, salt, normalized_iterations);
    record["password_algo"] = account_schema::PASSWORD_ALGO_PBKDF2_SHA256;
    record["password_salt"] = salt;
    record["password_iterations"] = normalized_iterations;
    record.erase("password");
}

void preserve_existing_password_material(nlohmann::json &record, const nlohmann::json &existing)
{
    if (json_record::has_nonempty_string_field(record, "password") ||
        json_record::has_nonempty_string_field(record, "password_hash"))
    {
        return;
    }
    record.erase("password");
    if (existing.contains("password_hash"))
    {
        record["password_hash"] = existing["password_hash"];
    }
    if (existing.contains("password_algo"))
    {
        record["password_algo"] = existing["password_algo"];
    }
    if (existing.contains("password_salt"))
    {
        record["password_salt"] = existing["password_salt"];
    }
    if (existing.contains("password_iterations"))
    {
        record["password_iterations"] = existing["password_iterations"];
    }
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

bool normalize_redeem_code(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!require_string(record, "code", error))
    {
        return false;
    }
    default_status(record);
    if (!validate_status(record, error))
    {
        return false;
    }
    record["amount_cents"] = record.value("amount_cents", 0);
    record["redeemed_count"] = record.value("redeemed_count", 0);
    record["max_redemptions"] = record.value("max_redemptions", 1);
    record["expire_time"] = record.value("expire_time", 0);
    if (!is_nonnegative_number(record["amount_cents"]) ||
        !is_nonnegative_number(record["redeemed_count"]) ||
        !is_nonnegative_number(record["max_redemptions"]) ||
        !is_nonnegative_number(record["expire_time"]))
    {
        return fail(error, "redeem code numeric fields must be non-negative");
    }
    if (json_record::as_i64(record["amount_cents"], 0) <= 0)
    {
        return fail(error, "amount_cents must be positive");
    }
    touch(record, now);
    return true;
}

bool normalize_redeem_redemption(nlohmann::json &record, std::int64_t now, std::string *error)
{
    if (!normalize_append_fact(record, "redemption_id", now, error) ||
        !require_string(record, "code", error) ||
        !require_string(record, "account_id", error) ||
        !require_string(record, "ledger_id", error))
    {
        return false;
    }
    record["amount_cents"] = record.value("amount_cents", 0);
    record["balance_after_cents"] = record.value("balance_after_cents", 0);
    if (!is_nonnegative_number(record["amount_cents"]) || !is_nonnegative_number(record["balance_after_cents"]))
    {
        return fail(error, "redeem redemption numeric fields must be non-negative");
    }
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
    nlohmann::json index = {
        {"schema_version", record.value("schema_version", CURRENT_SCHEMA_VERSION)},
        {"uid", record.value("username", std::string{})},
        {"account", record.value("username", std::string{})},
        {"access_account_id", record.value("access_account_id", std::string{})},
        {"access_username", record.value("username", std::string{})},
        {"access_kind", record.value("kind", std::string{})},
        {"access_status", record.value("status", std::string{})},
        {"owner_account_id", record.value("owner_account_id", std::string{})},
        {"owner_role", owner.value("role", std::string{})},
        {"owner_status", owner.value("status", std::string{})},
        {"mount_point_group_id", record.value("mount_point_group_id", std::string{})},
        {"group_uid", record.value("mount_point_group_id", std::string{})},
        {"connection_limit", record.value("concurrency_limit", 0)},
        {"account_concurrency_limit", owner.value("concurrency_limit", 0)},
        {"access_concurrency_limit", record.value("concurrency_limit", 0)},
        {"balance_cents", owner.value("balance_cents", 0)},
        {"credit_limit_cents", owner.value("credit_limit_cents", 0)},
        {"type", 0},
        {"state", string_value(record, "status") == STATUS_ACTIVE && string_value(owner, "status") == STATUS_ACTIVE ? 1 : 2},
        {"active", string_value(record, "status") == STATUS_ACTIVE && string_value(owner, "status") == STATUS_ACTIVE ? 1 : 0},
        {"expire_time", record.value("expire_time", 0LL)},
    };

    if (record.contains("password_hash"))
    {
        index["password_hash"] = record["password_hash"];
        index["password_algo"] = record.value("password_algo", std::string{});
        if (record.contains("password_salt"))
        {
            index["password_salt"] = record["password_salt"];
        }
        if (record.contains("password_iterations"))
        {
            index["password_iterations"] = record["password_iterations"];
        }
    }
    return index;
}

} // namespace navcaster::account_domain
