#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::account_schema
{

inline constexpr int CURRENT_SCHEMA_VERSION = 1;
inline constexpr const char *DEFAULT_GROUP_UID = "default";
inline constexpr int UNLIMITED_CONNECTIONS = 9999;

struct AccountAuthView
{
    std::string account;
    std::string password;
    std::string password_hash;
    std::string password_algo;
    std::string group_uid = DEFAULT_GROUP_UID;
    int connection_limit = UNLIMITED_CONNECTIONS;
    int state = 0;
    int active = 0;
    int type = 0;
    std::int64_t expire_time = 0;
    bool legacy_plain_password = false;
};

struct AccountSyncPlan
{
    std::string account;
    nlohmann::json record;
    bool write_active_index = false;
    bool delete_active_index = false;
    nlohmann::json active_index;
    std::string inactive_reason;
};

struct AccountDeletePlan
{
    std::string account;
    bool delete_record = false;
    bool delete_active_index = false;
};

std::string normalize_group_uid(const std::string &group_uid);
int normalize_connection_limit(int connection_limit);

nlohmann::json normalize_account_record(nlohmann::json record, std::int64_t now);
nlohmann::json build_active_index(const nlohmann::json &record);
bool build_account_sync_plan(nlohmann::json record, std::int64_t now, AccountSyncPlan &plan, std::string *error = nullptr);
bool build_account_delete_plan(const std::string &account, AccountDeletePlan &plan, std::string *error = nullptr);
bool is_login_enabled(const nlohmann::json &record, std::int64_t now, std::string *reason = nullptr);
bool parse_auth_view(const std::string &json_text, AccountAuthView &view, std::string *error = nullptr);
bool password_matches(const AccountAuthView &view, const std::string &password);

} // namespace navcaster::account_schema
