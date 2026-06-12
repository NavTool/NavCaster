#include "account_repository.h"
#include "account_schema.h"
#include "alias_repository.h"
#include "redis_keys.h"
#include "source_repository.h"

#include <cstdlib>
#include <iostream>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
int failures = 0;

class FakeRedisHashClient : public navcaster::storage::RedisHashClient
{
public:
    nlohmann::json hgetall(const char *key) override
    {
        auto key_it = hashes.find(key);
        if (key_it == hashes.end())
        {
            return nlohmann::json::object();
        }

        nlohmann::json result = nlohmann::json::object();
        for (const auto &[field, value] : key_it->second)
        {
            result[field] = value;
        }
        return result;
    }

    nlohmann::json hget(const char *key, const char *field) override
    {
        auto key_it = hashes.find(key);
        if (key_it == hashes.end())
        {
            return nullptr;
        }
        auto field_it = key_it->second.find(field);
        if (field_it == key_it->second.end())
        {
            return nullptr;
        }
        return field_it->second;
    }

    bool hset(const char *key, const char *field, const std::string &value) override
    {
        hashes[key][field] = parse_value(value);
        return true;
    }

    bool hsetnx(const char *key, const char *field, const std::string &value) override
    {
        auto &hash = hashes[key];
        if (hash.find(field) != hash.end())
        {
            return false;
        }
        hash[field] = parse_value(value);
        return true;
    }

    bool hdel(const char *key, const char *field) override
    {
        auto key_it = hashes.find(key);
        if (key_it == hashes.end())
        {
            return false;
        }
        return key_it->second.erase(field) > 0;
    }

    bool publish(const char *channel, const std::string &message) override
    {
        publishes.push_back({channel, message});
        return publish_ok;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, nlohmann::json>> hashes;
    std::vector<std::pair<std::string, std::string>> publishes;
    bool publish_ok = true;

private:
    static nlohmann::json parse_value(const std::string &value)
    {
        try
        {
            return nlohmann::json::parse(value);
        }
        catch (...)
        {
            return value;
        }
    }
};

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

void expect_has(const nlohmann::json &value, const char *field, const std::string &name)
{
    expect_true(value.contains(field), name);
}

void expect_missing(const nlohmann::json &value, const char *field, const std::string &name)
{
    expect_true(!value.contains(field), name);
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
    expect_missing(normalized, "password", "normalized account removes plaintext password");
    expect_eq(normalized.value("password_algo", std::string{}), account_schema::PASSWORD_ALGO_PBKDF2_SHA256, "normalized account password algo");
    expect_has(normalized, "password_hash", "normalized account password hash");
    expect_has(normalized, "password_salt", "normalized account password salt");
    expect_eq_int(normalized.value("password_iterations", 0), account_schema::DEFAULT_PASSWORD_ITERATIONS, "normalized account password iterations");

    std::string reason;
    expect_true(account_schema::is_login_enabled(normalized, 1000, &reason), "normalized account login enabled");
    expect_eq(
        account_schema::make_password_hash("password", "salt", 1),
        "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
        "pbkdf2 sha256 vector iter 1");
    expect_eq(
        account_schema::make_password_hash("password", "salt", 2),
        "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
        "pbkdf2 sha256 vector iter 2");

    account_schema::AccountSyncPlan sync_plan;
    expect_true(account_schema::build_account_sync_plan(account, 1000, sync_plan, &reason), "build account sync plan");
    expect_eq(sync_plan.account, "demo", "sync plan account");
    expect_true(sync_plan.write_active_index, "enabled account writes active index");
    expect_true(!sync_plan.delete_active_index, "enabled account keeps active index");
    expect_eq(sync_plan.record.value("uid", std::string{}), "demo", "sync plan record uid");
    expect_missing(sync_plan.record, "password", "sync plan record removes plaintext password");
    expect_has(sync_plan.record, "password_hash", "sync plan record password hash");
    expect_eq(sync_plan.active_index.value("account", std::string{}), "demo", "sync plan active account");
    expect_missing(sync_plan.active_index, "password", "sync plan active index removes plaintext password");
    expect_has(sync_plan.active_index, "password_hash", "sync plan active index password hash");
    expect_has(sync_plan.active_index, "password_salt", "sync plan active index password salt");
    expect_has(sync_plan.active_index, "password_iterations", "sync plan active index password iterations");

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
    expect_true(!active_index.value("legacy_plain_password", false), "active index is not legacy");

    account_schema::AccountAuthView view;
    expect_true(account_schema::parse_auth_view(active_index.dump(), view, &reason), "parse auth view");
    expect_eq(view.account, "demo", "auth view account");
    expect_eq_int(view.connection_limit, account_schema::UNLIMITED_CONNECTIONS, "unlimited connection normalized");
    expect_true(!view.legacy_plain_password, "auth view hashed password");
    expect_true(account_schema::password_matches(view, "secret"), "hashed password match");
    expect_true(!account_schema::password_matches(view, "wrong"), "hashed password mismatch");

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
    expect_true(view.legacy_plain_password, "legacy auth view marks plaintext password");
    expect_true(account_schema::password_matches(view, "secret"), "legacy password match");
    expect_true(!account_schema::password_matches(view, "wrong"), "legacy password mismatch");
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

    nlohmann::json deterministic_hash = {
        {"account", "hash-user"},
        {"password_hash", account_schema::make_password_hash("secret", "abcd", 2)},
        {"password_algo", account_schema::PASSWORD_ALGO_PBKDF2_SHA256},
        {"password_salt", "abcd"},
        {"password_iterations", 2},
        {"state", 1},
        {"active", 1}
    };
    expect_true(account_schema::build_account_sync_plan(deterministic_hash, 1000, sync_plan, &reason), "provided hash sync plan");
    expect_true(account_schema::parse_auth_view(sync_plan.active_index.dump(), view, &reason), "parse provided hash auth view");
    expect_true(account_schema::password_matches(view, "secret"), "provided hash password match");
    expect_true(!account_schema::password_matches(view, "wrong"), "provided hash password mismatch");

    nlohmann::json invalid_hash = {
        {"account", "invalid-hash"},
        {"password_hash", "not-enough"},
        {"password_algo", "sha256"},
        {"state", 1},
        {"active", 1}
    };
    expect_true(!account_schema::build_account_sync_plan(invalid_hash, 1000, sync_plan, &reason), "unsupported hash sync rejected");

    nlohmann::json current_hash = sync_plan.record;
    current_hash = {
        {"account", "demo"},
        {"password_hash", account_schema::make_password_hash("secret", "existing-salt", 2)},
        {"password_algo", account_schema::PASSWORD_ALGO_PBKDF2_SHA256},
        {"password_salt", "existing-salt"},
        {"password_iterations", 2}
    };
    nlohmann::json update_without_password = {
        {"account", "demo"},
        {"group_uid", "updated"},
        {"password", ""}
    };
    account_schema::preserve_existing_password_material(update_without_password, current_hash);
    expect_eq(update_without_password.value("password_hash", std::string{}), current_hash.value("password_hash", std::string{}), "update preserves existing hash");
    expect_missing(update_without_password, "password", "update removes empty password");

    nlohmann::json update_with_password = {
        {"account", "demo"},
        {"password", "new-secret"},
        {"password_hash", current_hash["password_hash"]},
        {"password_algo", current_hash["password_algo"]},
        {"password_salt", current_hash["password_salt"]},
        {"password_iterations", current_hash["password_iterations"]}
    };
    account_schema::preserve_existing_password_material(update_with_password, current_hash);
    expect_true(account_schema::build_account_sync_plan(update_with_password, 1000, sync_plan, &reason), "update new password sync plan");
    expect_missing(sync_plan.record, "password", "update new password removes plaintext");
    expect_true(sync_plan.record.value("password_hash", std::string{}) != current_hash.value("password_hash", std::string{}), "update new password rotates hash");
    expect_true(account_schema::parse_auth_view(sync_plan.active_index.dump(), view, &reason), "parse updated password auth view");
    expect_true(account_schema::password_matches(view, "new-secret"), "updated password matches");

    nlohmann::json current_legacy = {
        {"account", "legacy"},
        {"password", "old-secret"}
    };
    nlohmann::json legacy_update = {{"account", "legacy"}, {"remark", "kept"}};
    account_schema::preserve_existing_password_material(legacy_update, current_legacy);
    expect_eq(legacy_update.value("password", std::string{}), "old-secret", "update preserves legacy plaintext");
    expect_true(account_schema::build_account_sync_plan(legacy_update, 1000, sync_plan, &reason), "legacy update migrates to hash");
    expect_missing(sync_plan.record, "password", "legacy update removes plaintext after normalize");
    expect_has(sync_plan.record, "password_hash", "legacy update writes hash");

    nlohmann::json missing_account = {{"password", "secret"}};
    expect_true(!account_schema::build_account_sync_plan(missing_account, 1000, sync_plan, &reason), "missing account sync rejected");
    nlohmann::json missing_password = {{"account", "no-pass"}};
    expect_true(!account_schema::build_account_sync_plan(missing_password, 1000, sync_plan, &reason), "missing password sync rejected");

    account_schema::AccountDeletePlan delete_plan;
    expect_true(account_schema::build_account_delete_plan("demo", delete_plan, &reason), "build account delete plan");
    expect_eq(delete_plan.account, "demo", "delete plan account");
    expect_true(delete_plan.delete_record, "delete plan removes record");
    expect_true(delete_plan.delete_active_index, "delete plan removes active index");
    expect_true(!account_schema::build_account_delete_plan("", delete_plan, &reason), "empty delete plan rejected");

    FakeRedisHashClient fake_redis;
    navcaster::storage::AccountRepository account_repo(fake_redis);
    nlohmann::json repo_create = {
        {"account", "repo-user"},
        {"password", "secret"},
        {"state", 1},
        {"active", 1},
        {"connection_limit", 2}
    };
    auto repo_result = account_repo.create_account(repo_create, 1000);
    expect_true(repo_result.status == navcaster::storage::RepositoryStatus::Ok, "repository account create ok");
    expect_eq(repo_result.account, "repo-user", "repository account create account");
    expect_true(fake_redis.hget(navcaster::redis_keys::ACT_RECORD, "repo-user").is_object(), "repository writes account record");
    auto repo_active = fake_redis.hget(navcaster::redis_keys::ACT_ACTIVE, "repo-user");
    expect_true(repo_active.is_object(), "repository writes active index");
    expect_true(!repo_active.contains("password"), "repository active index has no plaintext password");
    expect_true(account_repo.create_account(repo_create, 1001).status == navcaster::storage::RepositoryStatus::Conflict, "repository duplicate create conflict");

    nlohmann::json repo_update = {
        {"account", "repo-user"},
        {"password", ""},
        {"state", 2},
        {"active", 1}
    };
    repo_result = account_repo.update_account("repo-user", repo_update, 1002);
    expect_true(repo_result.status == navcaster::storage::RepositoryStatus::Ok, "repository account update ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::ACT_ACTIVE, "repo-user").is_null(), "repository inactive update deletes active index");
    expect_true(repo_result.record.contains("password_hash"), "repository update preserves password material");

    nlohmann::json repo_mismatch = {{"account", "other"}, {"password", "secret"}};
    expect_true(account_repo.update_account("repo-user", repo_mismatch, 1003).status == navcaster::storage::RepositoryStatus::Invalid, "repository account mismatch rejected");
    expect_true(account_repo.update_account("missing-user", repo_create, 1003).status == navcaster::storage::RepositoryStatus::NotFound, "repository missing update rejected");

    repo_result = account_repo.delete_account("repo-user");
    expect_true(repo_result.status == navcaster::storage::RepositoryStatus::Ok, "repository delete ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::ACT_RECORD, "repo-user").is_null(), "repository delete removes record");
    expect_true(fake_redis.hget(navcaster::redis_keys::ACT_ACTIVE, "repo-user").is_null(), "repository delete removes active index");
    expect_true(account_repo.delete_account("repo-user").status == navcaster::storage::RepositoryStatus::NotFound, "repository missing delete not found");

    navcaster::storage::SourceRepositoryResult source_plan;
    nlohmann::json source_body = {{"mountpoint", "MOUNT1"}, {"format", "RTCM3"}};
    expect_true(navcaster::storage::build_source_create_plan(source_body, 2000, source_plan, &reason), "source create plan");
    expect_eq(source_plan.mountpoint, "MOUNT1", "source plan mountpoint");
    expect_eq(source_plan.record.value("uid", std::string{}), "MOUNT1", "source plan uid");
    expect_eq(source_plan.record.value("source_group_uid", std::string{}), "default", "source plan default group");
    expect_eq_int(source_plan.record.value("record_type", 0), 1, "source plan default record type");
    expect_eq_int(source_plan.record.value("decode_type", 0), 1, "source plan default decode type");
    expect_eq_int(source_plan.record.value("display_type", 0), 3, "source plan default display type");
    expect_true(source_plan.record.value("create_time", 0) == 2000, "source plan create time");
    expect_true(source_plan.record.value("update_time", 0) == 2000, "source plan update time");
    expect_true(!navcaster::storage::build_source_create_plan(nlohmann::json::object(), 2000, source_plan, &reason), "source missing mountpoint rejected");
    expect_true(!navcaster::storage::build_source_update_plan("MOUNT1", {{"mountpoint", "OTHER"}}, 2001, source_plan, &reason), "source update mountpoint mismatch rejected");

    navcaster::storage::SourceRepository source_repo(fake_redis);
    auto source_result = source_repo.create_source(source_body, 2000);
    expect_true(source_result.status == navcaster::storage::RepositoryStatus::Ok, "source repository create ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::MPT_RECORD, "MOUNT1").is_object(), "source repository writes record");
    expect_true(source_repo.create_source(source_body, 2001).status == navcaster::storage::RepositoryStatus::Conflict, "source repository duplicate conflict");
    source_result = source_repo.update_source("MOUNT1", {{"mountpoint", "MOUNT1"}, {"country", "CN"}}, 2002);
    expect_true(source_result.status == navcaster::storage::RepositoryStatus::Ok, "source repository update ok");
    expect_eq(fake_redis.hget(navcaster::redis_keys::MPT_RECORD, "MOUNT1").value("country", std::string{}), "CN", "source repository update value");
    expect_true(source_repo.delete_source("MOUNT1").status == navcaster::storage::RepositoryStatus::Ok, "source repository delete ok");
    expect_true(source_repo.delete_source("MOUNT1").status == navcaster::storage::RepositoryStatus::NotFound, "source repository delete missing");

    navcaster::storage::AliasRepositoryResult alias_plan;
    nlohmann::json alias_body = {{"alias_name", "ALIAS1"}, {"source_name", "MOUNT2"}};
    expect_true(navcaster::storage::build_alias_create_plan(alias_body, 3000, alias_plan, &reason), "alias create plan");
    expect_eq(alias_plan.uid, "ALIAS1", "alias plan uid fallback");
    expect_eq(alias_plan.rule.value("alias_name", std::string{}), "ALIAS1", "alias plan alias name");
    expect_eq(alias_plan.rule.value("source_name", std::string{}), "MOUNT2", "alias plan source name");
    expect_true(alias_plan.rule.value("enable", false), "alias plan enable default");
    expect_true(alias_plan.rule.value("visible", false), "alias plan visible default");
    expect_true(alias_plan.rule.value("create_time", 0) == 3000, "alias plan create time");
    expect_true(!navcaster::storage::build_alias_create_plan({{"alias_name", "NO_SOURCE"}}, 3000, alias_plan, &reason), "alias missing source rejected");
    expect_true(navcaster::storage::build_alias_update_plan("URL_ALIAS", {{"alias_name", "BODY_ALIAS"}, {"source_name", "MOUNT3"}}, 3001, alias_plan, &reason), "alias update plan");
    expect_eq(alias_plan.uid, "URL_ALIAS", "alias update uses URL uid");
    expect_eq(alias_plan.rule.value("uid", std::string{}), "URL_ALIAS", "alias update writes URL uid");

    navcaster::storage::AliasRepository alias_repo(fake_redis);
    auto alias_result = alias_repo.create_alias(alias_body, 3000);
    expect_true(alias_result.status == navcaster::storage::RepositoryStatus::Ok, "alias repository create ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::ALIAS_RULE, "ALIAS1").is_object(), "alias repository writes rule");
    expect_true(!fake_redis.publishes.empty(), "alias repository publishes config change");
    expect_eq(fake_redis.publishes.back().first, navcaster::redis_keys::CASTER_CONF, "alias publish channel");
    expect_eq(fake_redis.publishes.back().second, "ALIAS", "alias publish payload");
    expect_true(alias_repo.create_alias(alias_body, 3001).status == navcaster::storage::RepositoryStatus::Conflict, "alias repository duplicate conflict");
    alias_result = alias_repo.update_alias("ALIAS1", {{"source_name", "MOUNT4"}}, 3002);
    expect_true(alias_result.status == navcaster::storage::RepositoryStatus::Ok, "alias repository update ok");
    expect_eq(fake_redis.hget(navcaster::redis_keys::ALIAS_RULE, "ALIAS1").value("source_name", std::string{}), "MOUNT4", "alias repository update source");
    expect_true(alias_repo.delete_alias("ALIAS1").status == navcaster::storage::RepositoryStatus::Ok, "alias repository delete ok");
    expect_true(alias_repo.delete_alias("ALIAS1").status == navcaster::storage::RepositoryStatus::NotFound, "alias repository missing delete");

    if (failures != 0)
    {
        std::cerr << "[schema_smoke] failures: " << failures << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "[schema_smoke] all checks passed\n";
    return EXIT_SUCCESS;
}
