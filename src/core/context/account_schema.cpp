#include "account_schema.h"

#include <utility>

namespace navcaster::account_schema
{
namespace
{
constexpr int ACCOUNT_STATE_TYPE_NORMAL = 1;
constexpr int ACCOUNT_ACTIVE_STATE_ACTIVE = 1;

std::int64_t number_to_i64(const nlohmann::json &value, std::int64_t fallback = 0)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return value.get<std::int64_t>();
    }
    if (value.is_number_float())
    {
        return static_cast<std::int64_t>(value.get<double>());
    }
    return fallback;
}

int number_to_int(const nlohmann::json &value, int fallback = 0)
{
    if (value.is_number_integer() || value.is_number_unsigned())
    {
        return value.get<int>();
    }
    if (value.is_number_float())
    {
        return static_cast<int>(value.get<double>());
    }
    return fallback;
}

int bool_or_number_to_int(const nlohmann::json &value, int fallback = 0)
{
    if (value.is_boolean())
    {
        return value.get<bool>() ? 1 : 0;
    }
    return number_to_int(value, fallback);
}

std::string string_or_empty(const nlohmann::json &record, const char *field)
{
    auto it = record.find(field);
    if (it == record.end() || !it->is_string())
    {
        return {};
    }
    return it->get<std::string>();
}
} // namespace

std::string normalize_group_uid(const std::string &group_uid)
{
    return group_uid.empty() ? DEFAULT_GROUP_UID : group_uid;
}

int normalize_connection_limit(int connection_limit)
{
    return connection_limit <= 0 ? UNLIMITED_CONNECTIONS : connection_limit;
}

nlohmann::json normalize_account_record(nlohmann::json record, std::int64_t now)
{
    const std::string account = record.value("account", record.value("uid", std::string{}));
    if (!account.empty())
    {
        record["account"] = account;
        if (!record.contains("uid") || record.value("uid", std::string{}).empty())
        {
            record["uid"] = account;
        }
    }

    record["schema_version"] = record.value("schema_version", CURRENT_SCHEMA_VERSION);
    record["group_uid"] = normalize_group_uid(record.value("group_uid", record.value("group", std::string{})));
    record["connection_limit"] = record.value("connection_limit", record.value("connect_limit", 0));

    if (!record.contains("create_time") || number_to_i64(record["create_time"]) <= 0)
    {
        record["create_time"] = now;
    }
    record["update_time"] = now;

    return record;
}

nlohmann::json build_active_index(const nlohmann::json &record)
{
    nlohmann::json active;
    active["schema_version"] = record.value("schema_version", CURRENT_SCHEMA_VERSION);
    active["uid"] = record.value("uid", record.value("account", std::string{}));
    active["account"] = record.value("account", active.value("uid", std::string{}));
    active["group_uid"] = normalize_group_uid(record.value("group_uid", record.value("group", std::string{})));
    active["connection_limit"] = record.value("connection_limit", record.value("connect_limit", 0));
    active["type"] = record.value("type", 0);
    active["state"] = record.value("state", 0);
    active["active"] = record.value("active", 0);
    active["expire_time"] = record.value("expire_time", record.value("expire", 0.0));

    if (record.contains("password_hash"))
    {
        active["password_hash"] = record["password_hash"];
        active["password_algo"] = record.value("password_algo", std::string{});
    }
    else if (record.contains("password"))
    {
        active["password"] = record["password"];
        active["legacy_plain_password"] = true;
    }

    return active;
}

bool build_account_sync_plan(nlohmann::json record, std::int64_t now, AccountSyncPlan &plan, std::string *error)
{
    plan = {};
    auto normalized = normalize_account_record(std::move(record), now);
    const std::string account = normalized.value("account", std::string{});
    if (account.empty())
    {
        if (error)
        {
            *error = "account is required";
        }
        return false;
    }

    plan.account = account;
    plan.record = std::move(normalized);

    std::string reason;
    if (is_login_enabled(plan.record, now, &reason))
    {
        plan.write_active_index = true;
        plan.active_index = build_active_index(plan.record);
    }
    else
    {
        plan.delete_active_index = true;
        plan.inactive_reason = std::move(reason);
    }

    return true;
}

bool build_account_delete_plan(const std::string &account, AccountDeletePlan &plan, std::string *error)
{
    plan = {};
    if (account.empty())
    {
        if (error)
        {
            *error = "account is required";
        }
        return false;
    }

    plan.account = account;
    plan.delete_record = true;
    plan.delete_active_index = true;
    return true;
}

bool is_login_enabled(const nlohmann::json &record, std::int64_t now, std::string *reason)
{
    const int state = record.value("state", 0);
    if (state != 0 && state != ACCOUNT_STATE_TYPE_NORMAL)
    {
        if (reason)
        {
            *reason = "account state is not normal";
        }
        return false;
    }

    const int active = record.value("active", 0);
    if (active != 0 && active != ACCOUNT_ACTIVE_STATE_ACTIVE)
    {
        if (reason)
        {
            *reason = "account is not active";
        }
        return false;
    }

    auto expire_it = record.find("expire_time");
    if (expire_it == record.end())
    {
        expire_it = record.find("expire");
    }
    const std::int64_t expire_time = expire_it == record.end() ? 0 : number_to_i64(*expire_it);
    if (expire_time > 0 && now > expire_time)
    {
        if (reason)
        {
            *reason = "account expired";
        }
        return false;
    }

    return true;
}

bool parse_auth_view(const std::string &json_text, AccountAuthView &view, std::string *error)
{
    nlohmann::json record;
    try
    {
        record = nlohmann::json::parse(json_text);
    }
    catch (const std::exception &e)
    {
        if (error)
        {
            *error = e.what();
        }
        return false;
    }

    view.account = record.value("account", record.value("uid", std::string{}));
    view.password = string_or_empty(record, "password");
    view.password_hash = string_or_empty(record, "password_hash");
    view.password_algo = string_or_empty(record, "password_algo");
    view.group_uid = normalize_group_uid(record.value("group_uid", record.value("group", std::string{})));
    view.connection_limit = normalize_connection_limit(record.value("connection_limit", record.value("connect_limit", 0)));
    view.type = record.value("type", 0);
    auto state_it = record.find("state");
    view.state = state_it == record.end() ? 0 : bool_or_number_to_int(*state_it);
    auto active_it = record.find("active");
    view.active = active_it == record.end() ? 0 : bool_or_number_to_int(*active_it);

    auto expire_it = record.find("expire_time");
    if (expire_it == record.end())
    {
        expire_it = record.find("expire");
    }
    view.expire_time = expire_it == record.end() ? 0 : number_to_i64(*expire_it);
    view.legacy_plain_password = view.password_hash.empty() && !view.password.empty();
    return true;
}

bool password_matches(const AccountAuthView &view, const std::string &password)
{
    if (view.legacy_plain_password)
    {
        return view.password == password;
    }

    // Hash verification is introduced in a later iteration. Keep this helper
    // explicit so callers do not silently accept unsupported hash formats.
    return false;
}

} // namespace navcaster::account_schema
