#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::account_domain
{

inline constexpr int CURRENT_SCHEMA_VERSION = 1;
inline constexpr const char *ROLE_ADMIN = "admin";
inline constexpr const char *ROLE_USER = "user";
inline constexpr const char *ROLE_SUPPLIER = "supplier";
inline constexpr const char *STATUS_ACTIVE = "active";
inline constexpr const char *STATUS_DISABLED = "disabled";
inline constexpr const char *STATUS_DELETED = "deleted";
inline constexpr const char *ACCESS_KIND_USER_CLIENT = "user_client";
inline constexpr const char *ACCESS_KIND_SUPPLIER_STATION = "supplier_station";

bool is_account_role(const std::string &role);
bool is_resource_status(const std::string &status);
bool is_active_status(const nlohmann::json &record);
bool is_access_account_kind(const std::string &kind);
bool is_access_kind_allowed_for_role(const std::string &role, const std::string &kind);
bool is_nonnegative_number(const nlohmann::json &value);
bool has_nonempty_string(const nlohmann::json &record, const char *field);

bool normalize_account_record(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_mount_point_group(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_mount_point(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_access_account(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
void normalize_password_material(nlohmann::json &record);
void preserve_existing_password_material(nlohmann::json &record, const nlohmann::json &existing);
bool normalize_subscription(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_station_record(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_station_event(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_balance_ledger_entry(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_billing_usage_entry(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_data_push_usage(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);
bool normalize_supplier_supply_usage(nlohmann::json &record, std::int64_t now, std::string *error = nullptr);

nlohmann::json username_index_record(const std::string &id, const std::string &status, std::int64_t now);
nlohmann::json access_account_owner_summary(const nlohmann::json &record);
nlohmann::json access_account_auth_index(const nlohmann::json &record, const nlohmann::json &owner);

} // namespace navcaster::account_domain
