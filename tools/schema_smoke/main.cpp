#include "account_schema.h"
#include "redis_keys.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void expect_true(bool value, const std::string &name)
{
    if (!value)
    {
        ++failures;
        std::cerr << "[FAIL] " << name << '\n';
    }
}

void expect_eq(const std::string &actual, const std::string &expected, const std::string &name)
{
    if (actual != expected)
    {
        ++failures;
        std::cerr << "[FAIL] " << name << ": expected [" << expected << "], got [" << actual << "]\n";
    }
}

void expect_eq_int(int actual, int expected, const std::string &name)
{
    if (actual != expected)
    {
        ++failures;
        std::cerr << "[FAIL] " << name << ": expected [" << expected << "], got [" << actual << "]\n";
    }
}
} // namespace

int main()
{
    using namespace navcaster;

    expect_eq(redis_keys::mpt_rec("BASE01"), "MPT:REC:BASE01", "mpt_rec key");
    expect_eq(redis_keys::access_item("default"), "ACCESS:ITEM:default", "access item key");
    expect_eq(redis_keys::act_session("rover"), "ACT:SESSION:rover", "account session key");
    expect_eq(redis_keys::node_history_1m("Node_abc"), "NODE:HISTORY:Node_abc:1M", "node history 1m key");
    expect_eq(redis_keys::mpt_channel("BASE01"), "MPT:BASE01", "mpt channel");
    expect_eq(redis_keys::ACT_RECORD, "ACT:RECORD", "account record key");
    expect_eq(redis_keys::ACT_ACTIVE, "ACT:ACTIVE", "account login index key");
    expect_eq(redis_keys::STR_ACTIVE_LEGACY, "STR:ACTIVE", "legacy active session key");

    nlohmann::json account = {
        {"account", "demo"},
        {"password", "secret"},
        {"state", 1},
        {"active", 1},
        {"connection_limit", 0}
    };

    auto normalized = account_schema::normalize_account_record(account, 1000);
    expect_eq(normalized.value("uid", std::string{}), "demo", "account uid default");
    expect_eq(normalized.value("group_uid", std::string{}), "default", "account group default");
    expect_eq_int(normalized.value("schema_version", 0), account_schema::CURRENT_SCHEMA_VERSION, "schema version");
    expect_true(normalized.value("create_time", 0) == 1000, "create time filled");
    expect_true(normalized.value("update_time", 0) == 1000, "update time filled");

    std::string reason;
    expect_true(account_schema::is_login_enabled(normalized, 1000, &reason), "normalized account login enabled");

    account_schema::AccountSyncPlan sync_plan;
    expect_true(account_schema::build_account_sync_plan(account, 1000, sync_plan, &reason), "build account sync plan");
    expect_eq(sync_plan.account, "demo", "sync plan account");
    expect_true(sync_plan.write_active_index, "enabled account writes active index");
    expect_true(!sync_plan.delete_active_index, "enabled account keeps active index");
    expect_eq(sync_plan.record.value("uid", std::string{}), "demo", "sync plan record uid");
    expect_eq(sync_plan.active_index.value("account", std::string{}), "demo", "sync plan active account");

    nlohmann::json uid_only = {
        {"uid", "legacy"},
        {"password", "secret"},
        {"state", 1},
        {"active", 1}
    };
    expect_true(account_schema::build_account_sync_plan(uid_only, 1000, sync_plan, &reason), "uid-only account sync plan");
    expect_eq(sync_plan.account, "legacy", "uid-only account field");
    expect_eq(sync_plan.record.value("account", std::string{}), "legacy", "uid-only normalized account");

    nlohmann::json account_uid_conflict = {
        {"uid", "internal-id"},
        {"account", "login-name"},
        {"password", "secret"},
        {"state", 1},
        {"active", 1}
    };
    expect_true(account_schema::build_account_sync_plan(account_uid_conflict, 1000, sync_plan, &reason), "account uid conflict sync plan");
    expect_eq(sync_plan.account, "login-name", "account uid conflict uses account field");
    expect_eq(sync_plan.record.value("uid", std::string{}), "internal-id", "account uid conflict preserves uid");

    auto active_index = account_schema::build_active_index(normalized);
    expect_eq(active_index.value("account", std::string{}), "demo", "active index account");
    expect_eq(active_index.value("group_uid", std::string{}), "default", "active index group");
    expect_true(active_index.value("legacy_plain_password", false), "legacy password marker");

    account_schema::AccountAuthView view;
    expect_true(account_schema::parse_auth_view(active_index.dump(), view, &reason), "parse auth view");
    expect_eq(view.account, "demo", "auth view account");
    expect_eq_int(view.connection_limit, account_schema::UNLIMITED_CONNECTIONS, "unlimited connection normalized");
    expect_true(view.legacy_plain_password, "auth view legacy password");
    expect_true(account_schema::password_matches(view, "secret"), "legacy password match");
    expect_true(!account_schema::password_matches(view, "wrong"), "legacy password mismatch");

    nlohmann::json legacy_auth = {
        {"account", "legacy"},
        {"password", "secret"},
        {"active", true},
        {"connect_limit", 3},
        {"group", "legacy-group"},
        {"expire", 2000}
    };
    expect_true(account_schema::parse_auth_view(legacy_auth.dump(), view, &reason), "parse legacy auth view");
    expect_eq(view.account, "legacy", "legacy auth account");
    expect_eq(view.group_uid, "legacy-group", "legacy auth group");
    expect_eq_int(view.connection_limit, 3, "legacy auth connect limit");
    expect_eq_int(view.active, 1, "legacy auth boolean active");
    expect_true(view.expire_time == 2000, "legacy auth expire");
    expect_true(!account_schema::parse_auth_view("{broken-json", view, &reason), "invalid auth json rejected");

    auto expired = normalized;
    expired["expire_time"] = 999;
    expect_true(!account_schema::is_login_enabled(expired, 1000, &reason), "expired account rejected");
    expect_true(account_schema::build_account_sync_plan(expired, 1000, sync_plan, &reason), "build expired account sync plan");
    expect_true(!sync_plan.write_active_index, "expired account skips active index");
    expect_true(sync_plan.delete_active_index, "expired account deletes active index");
    expect_eq(sync_plan.inactive_reason, "account expired", "expired account inactive reason");

    auto frozen = normalized;
    frozen["state"] = 2;
    expect_true(account_schema::build_account_sync_plan(frozen, 1000, sync_plan, &reason), "build frozen account sync plan");
    expect_true(sync_plan.delete_active_index, "frozen account deletes active index");

    auto hashed = normalized;
    hashed.erase("password");
    hashed["password_hash"] = "sha256:placeholder";
    hashed["password_algo"] = "sha256";
    auto hashed_index = account_schema::build_active_index(hashed);
    expect_true(account_schema::parse_auth_view(hashed_index.dump(), view, &reason), "parse hashed auth view");
    expect_true(!view.legacy_plain_password, "hashed auth view is not legacy");
    expect_true(!account_schema::password_matches(view, "secret"), "unsupported hash does not match");

    hashed["password"] = "secret";
    hashed_index = account_schema::build_active_index(hashed);
    expect_true(account_schema::parse_auth_view(hashed_index.dump(), view, &reason), "parse mixed hash and password auth view");
    expect_true(!view.legacy_plain_password, "hash takes priority over legacy password");
    expect_true(!account_schema::password_matches(view, "secret"), "hash priority avoids plaintext fallback");

    nlohmann::json missing_account = {{"password", "secret"}};
    expect_true(!account_schema::build_account_sync_plan(missing_account, 1000, sync_plan, &reason), "missing account sync rejected");

    account_schema::AccountDeletePlan delete_plan;
    expect_true(account_schema::build_account_delete_plan("demo", delete_plan, &reason), "build account delete plan");
    expect_eq(delete_plan.account, "demo", "delete plan account");
    expect_true(delete_plan.delete_record, "delete plan removes record");
    expect_true(delete_plan.delete_active_index, "delete plan removes active index");
    expect_true(!account_schema::build_account_delete_plan("", delete_plan, &reason), "empty delete plan rejected");

    if (failures != 0)
    {
        std::cerr << "[schema_smoke] failures: " << failures << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "[schema_smoke] all checks passed\n";
    return EXIT_SUCCESS;
}
