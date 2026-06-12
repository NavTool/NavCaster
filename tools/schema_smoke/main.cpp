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

    auto expired = normalized;
    expired["expire_time"] = 999;
    expect_true(!account_schema::is_login_enabled(expired, 1000, &reason), "expired account rejected");

    if (failures != 0)
    {
        std::cerr << "[schema_smoke] failures: " << failures << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "[schema_smoke] all checks passed\n";
    return EXIT_SUCCESS;
}
