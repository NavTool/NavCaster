#include "account_repository.h"
#include "account_controller.h"
#include "account_schema.h"
#include "access_controller.h"
#include "access_repository.h"
#include "alias_controller.h"
#include "alias_repository.h"
#include "broadcast_msg.h"
#include "config_repository.h"
#include "config_controller.h"
#include "connection_history_repository.h"
#include "connection_history_service.h"
#include "json_record.h"
#include "node_history_repository.h"
#include "node_history_service.h"
#include "redis_keys.h"
#include "relay_controller.h"
#include "relay_repository.h"
#include "runtime_command_service.h"
#include "runtime_state_controller.h"
#include "runtime_state_repository.h"
#include "sourcetable_service.h"
#include "source_controller.h"
#include "source_repository.h"
#include "statistics_service.h"
#include "sse_snapshot_service.h"

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

    nlohmann::json get(const char *key) override
    {
        auto it = strings.find(key);
        if (it == strings.end())
        {
            return nullptr;
        }
        return it->second;
    }

    bool set(const char *key, const std::string &value) override
    {
        strings[key] = parse_value(value);
        return set_ok;
    }

    bool publish(const char *channel, const std::string &message) override
    {
        publishes.push_back({channel, message});
        return publish_ok;
    }

    nlohmann::json scan_hgetall_prefix(const char *prefix) override
    {
        nlohmann::json result = nlohmann::json::object();
        const std::string prefix_text = prefix;
        for (const auto &[key, hash] : hashes)
        {
            if (key.rfind(prefix_text, 0) != 0)
            {
                continue;
            }
            for (const auto &[field, value] : hash)
            {
                result[field] = value;
            }
        }
        return result;
    }

    nlohmann::json lrange(const char *key, long long start, long long stop) override
    {
        auto key_it = lists.find(key);
        if (key_it == lists.end())
        {
            return nlohmann::json::array();
        }

        nlohmann::json result = nlohmann::json::array();
        const auto &items = key_it->second;
        if (items.empty() || stop < start)
        {
            return result;
        }
        if (start < 0)
        {
            start = 0;
        }
        if (stop >= static_cast<long long>(items.size()))
        {
            stop = static_cast<long long>(items.size()) - 1;
        }
        for (long long i = start; i <= stop; ++i)
        {
            result.push_back(items[static_cast<std::size_t>(i)]);
        }
        return result;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, nlohmann::json>> hashes;
    std::unordered_map<std::string, nlohmann::json> strings;
    std::unordered_map<std::string, std::vector<nlohmann::json>> lists;
    std::vector<std::pair<std::string, std::string>> publishes;
    bool set_ok = true;
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
    expect_eq(redis_keys::node_history("Node_abc"), "NODE:HISTORY:Node_abc", "node history raw key");
    expect_eq(redis_keys::node_history_1m("Node_abc"), "NODE:HISTORY:Node_abc:1M", "node history 1m key");
    expect_eq(redis_keys::node_history_5m("Node_abc"), "NODE:HISTORY:Node_abc:5M", "node history 5m key");
    expect_eq(redis_keys::mpt_channel("BASE01"), "MPT:BASE01", "mpt channel");
    expect_eq(redis_keys::ACT_RECORD, "ACT:RECORD", "account record key");
    expect_eq(redis_keys::ACT_ACTIVE, "ACT:ACTIVE", "account login index key");
    expect_eq(redis_keys::STR_ACTIVE_LEGACY, "STR:ACTIVE", "legacy active session key");
    expect_eq(redis_keys::LOG_MPT_PREFIX, "LOG:MPT:", "connection history mpt prefix");
    expect_eq(redis_keys::LOG_USR_PREFIX, "LOG:USR:", "connection history usr prefix");

    FakeRedisHashClient history_redis;
    history_redis.hashes[redis_keys::log_mpt("MOUNT_A")]["session-a"] = {{"name", "MOUNT_A"}, {"connect_time", 1000}};
    history_redis.hashes[redis_keys::log_mpt("MOUNT_A")]["session-c"] = {{"name", "MOUNT_A"}, {"connect_time", 3000}, {"disconnect_time", 3500}};
    history_redis.hashes[redis_keys::log_mpt("MOUNT_A")]["ignored"] = "not-an-object";
    history_redis.hashes[redis_keys::log_mpt("MOUNT_B")]["session-b"] = {{"name", "MOUNT_B"}, {"connect_time", 2000}};
    history_redis.hashes[redis_keys::log_usr("user1")]["user-session"] = {{"name", "user1"}, {"connect_time", 3000}};
    history_redis.hashes[redis_keys::MPT_STAT]["online"] = {{"ignored", true}};
    navcaster::storage::ConnectionHistoryRepository history_repo(history_redis);
    auto server_history = history_repo.list(navcaster::storage::ConnectionHistoryKind::Server);
    expect_true(server_history.contains("session-a"), "connection history repository lists server session");
    expect_true(server_history.contains("session-b"), "connection history repository lists second server session");
    expect_true(!server_history.contains("user-session"), "connection history repository excludes user session");
    auto client_history = history_repo.list(navcaster::storage::ConnectionHistoryKind::Client);
    expect_true(client_history.contains("user-session"), "connection history repository lists client session");
    expect_eq(navcaster::storage::connection_history_prefix(navcaster::storage::ConnectionHistoryKind::Server), redis_keys::LOG_MPT_PREFIX, "connection history server prefix");
    expect_eq(navcaster::storage::connection_history_prefix(navcaster::storage::ConnectionHistoryKind::Client), redis_keys::LOG_USR_PREFIX, "connection history client prefix");
    navcaster::http_api::ConnectionHistoryService history_service(history_redis);
    auto history_response = history_service.list(navcaster::storage::ConnectionHistoryKind::Server);
    expect_eq_int(history_response.status_code, 200, "connection history service status");
    auto history_body = nlohmann::json::parse(history_response.body);
    expect_true(history_body.contains("session-a"), "connection history service body");
    auto history_detail_response = history_service.detail(navcaster::storage::ConnectionHistoryKind::Server, "MOUNT_A", 5000);
    expect_eq_int(history_detail_response.status_code, 200, "connection history service detail status");
    auto history_detail = nlohmann::json::parse(history_detail_response.body);
    expect_eq_int(static_cast<int>(history_detail.size()), 2, "connection history service detail ignores non-object");
    expect_eq_int(history_detail[0].value("connect_time", 0), 3000, "connection history service detail sorted newest first");
    expect_true(!history_detail[0].value("online", true), "connection history service detail offline flag");
    expect_eq_int(history_detail[0].value("duration", 0), 500, "connection history service detail offline duration");
    expect_true(history_detail[1].value("online", false), "connection history service detail online flag");
    expect_eq_int(history_detail[1].value("duration", 0), 4000, "connection history service detail online duration");
    auto client_detail_response = history_service.detail(navcaster::storage::ConnectionHistoryKind::Client, "user1", 5000);
    expect_eq_int(client_detail_response.status_code, 200, "connection history service client detail status");
    auto client_detail = nlohmann::json::parse(client_detail_response.body);
    expect_eq_int(client_detail[0].value("duration", 0), 2000, "connection history service client online duration");
    expect_eq_int(history_service.detail(navcaster::storage::ConnectionHistoryKind::Server, "", 5000).status_code, 400, "connection history service missing mount");
    expect_eq_int(history_service.detail(navcaster::storage::ConnectionHistoryKind::Client, "", 5000).status_code, 400, "connection history service missing user");

    FakeRedisHashClient node_history_redis;
    node_history_redis.lists[redis_keys::node_history("node-1")] = {
        {{"ts", 3}},
        {{"ts", 2}},
        {{"ts", 1}}
    };
    node_history_redis.lists[redis_keys::node_history_1m("node-1")] = {
        {{"bucket", "1m-a"}},
        {{"bucket", "1m-b"}}
    };
    node_history_redis.lists[redis_keys::node_history_5m("node-1")] = {
        {{"bucket", "5m-a"}}
    };
    expect_true(navcaster::storage::parse_node_history_range("raw") == navcaster::storage::NodeHistoryRange::Raw, "node history parses raw");
    expect_true(navcaster::storage::parse_node_history_range("1m") == navcaster::storage::NodeHistoryRange::OneMinute, "node history parses 1m");
    expect_true(navcaster::storage::parse_node_history_range("5m") == navcaster::storage::NodeHistoryRange::FiveMinutes, "node history parses 5m");
    expect_true(navcaster::storage::parse_node_history_range("bad") == navcaster::storage::NodeHistoryRange::Raw, "node history bad range falls back raw");
    expect_eq_int(static_cast<int>(navcaster::storage::normalize_node_history_limit(0, navcaster::storage::NodeHistoryRange::Raw)), 17280, "node history zero limit default");
    expect_eq_int(static_cast<int>(navcaster::storage::normalize_node_history_limit(999999, navcaster::storage::NodeHistoryRange::FiveMinutes)), 8640, "node history 5m limit cap");
    expect_eq(navcaster::storage::node_history_key("node-1", navcaster::storage::NodeHistoryRange::OneMinute), redis_keys::node_history_1m("node-1"), "node history repository 1m key");
    navcaster::storage::NodeHistoryRepository node_history_repo(node_history_redis);
    auto node_history_raw = node_history_repo.list("node-1", navcaster::storage::NodeHistoryRange::Raw, 2);
    expect_eq_int(static_cast<int>(node_history_raw.size()), 2, "node history repository applies limit");
    expect_eq_int(node_history_raw[0].value("ts", 0), 3, "node history repository keeps order");
    navcaster::http_api::NodeHistoryService node_history_service(node_history_redis);
    auto node_history_response = node_history_service.list("node-1", "1m", 5);
    expect_eq_int(node_history_response.status_code, 200, "node history service status");
    auto node_history_body = nlohmann::json::parse(node_history_response.body);
    expect_eq(node_history_body[0].value("bucket", std::string{}), "1m-a", "node history service range");
    node_history_response = node_history_service.list("node-1", "5m", 1);
    node_history_body = nlohmann::json::parse(node_history_response.body);
    expect_eq(node_history_body[0].value("bucket", std::string{}), "5m-a", "node history service 5m range");
    expect_eq_int(node_history_service.list("", "raw", 1).status_code, 400, "node history service missing node");

    nlohmann::json helper_record = {{"uid", "helper"}, {"create_time", 0}};
    json_record::touch_timestamps(helper_record, 1234);
    expect_eq_int(helper_record.value("create_time", 0), 1234, "json helper fills create time");
    expect_eq_int(helper_record.value("update_time", 0), 1234, "json helper fills update time");
    json_record::touch_timestamps(helper_record, 2345);
    expect_eq_int(helper_record.value("create_time", 0), 1234, "json helper preserves create time");
    expect_eq_int(helper_record.value("update_time", 0), 2345, "json helper refreshes update time");
    expect_eq_int(json_record::as_i64(12.9), 12, "json helper float to i64");
    expect_eq_int(json_record::as_int("x", 7), 7, "json helper int fallback");
    expect_eq_int(json_record::bool_or_number_as_int(true), 1, "json helper bool true");
    expect_eq(json_record::string_field(helper_record, "uid"), "helper", "json helper string field");
    nlohmann::json parsed_record;
    expect_true(json_record::parse_record(json_record::dump_record(helper_record), parsed_record), "json helper parse dumped record");
    expect_true(parsed_record.is_object(), "json helper parsed record is object");
    expect_true(json_record::coerce_record(json_record::dump_record(helper_record), parsed_record), "json helper coerces stringified object");
    expect_true(!json_record::coerce_record(nlohmann::json::array(), parsed_record), "json helper rejects array record");
    expect_true(!json_record::parse_record("{", parsed_record), "json helper rejects broken json");

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

    FakeRedisHashClient account_controller_redis;
    navcaster::http_api::AccountController account_controller(account_controller_redis, 12000);
    auto account_response = account_controller.list_accounts();
    expect_eq_int(account_response.status_code, 200, "account controller list ok");
    account_response = account_controller.create_account(R"({"account":"ctrl-user","password":"secret","state":1,"active":1})");
    expect_eq_int(account_response.status_code, 201, "account controller create ok");
    auto account_controller_body = nlohmann::json::parse(account_response.body);
    expect_eq(account_controller_body.value("account", std::string{}), "ctrl-user", "account controller create response");
    auto controller_record = account_controller_redis.hget(navcaster::redis_keys::ACT_RECORD, "ctrl-user");
    expect_true(controller_record.is_object(), "account controller writes account record");
    expect_missing(controller_record, "password", "account controller create removes plaintext");
    expect_has(controller_record, "password_hash", "account controller create hashes password");
    expect_eq_int(controller_record.value("create_time", 0), 12000, "account controller create timestamp");
    auto controller_active = account_controller_redis.hget(navcaster::redis_keys::ACT_ACTIVE, "ctrl-user");
    expect_true(controller_active.is_object(), "account controller writes active index");
    expect_missing(controller_active, "password", "account controller active index removes plaintext");
    account_response = account_controller.get_account("ctrl-user");
    expect_eq_int(account_response.status_code, 200, "account controller get ok");
    account_response = account_controller.list_accounts();
    account_controller_body = nlohmann::json::parse(account_response.body);
    expect_true(account_controller_body.contains("ctrl-user"), "account controller list contains account");
    account_response = account_controller.create_account(R"({"account":"ctrl-user","password":"secret"})");
    expect_eq_int(account_response.status_code, 409, "account controller duplicate create");
    account_response = account_controller.create_account("{");
    expect_eq_int(account_response.status_code, 400, "account controller create invalid json");
    account_response = account_controller.create_account(R"({"account":"bad"})");
    expect_eq_int(account_response.status_code, 400, "account controller create missing password");
    account_response = account_controller.get_account("missing");
    expect_eq_int(account_response.status_code, 404, "account controller missing get");
    account_controller_redis.hset(navcaster::redis_keys::STR_ACTIVE_LEGACY, "session-user", nlohmann::json{{"account", "session-user"}}.dump());
    account_response = account_controller.get_account("active");
    expect_eq_int(account_response.status_code, 200, "account controller active special get ok");
    account_controller_body = nlohmann::json::parse(account_response.body);
    expect_true(account_controller_body.contains("session-user"), "account controller active special reads sessions");
    account_response = account_controller.get_account("");
    expect_eq_int(account_response.status_code, 200, "account controller empty get lists active sessions");
    account_response = account_controller.list_active_sessions();
    expect_eq_int(account_response.status_code, 200, "account controller list active sessions ok");
    account_response = account_controller.update_account("ctrl-user", R"({"password":"","state":1,"active":1,"connection_limit":4})");
    expect_eq_int(account_response.status_code, 200, "account controller update keeps password ok");
    auto controller_record_after_keep = account_controller_redis.hget(navcaster::redis_keys::ACT_RECORD, "ctrl-user");
    expect_eq(controller_record_after_keep.value("password_hash", std::string{}), controller_record.value("password_hash", std::string{}), "account controller empty password keeps hash");
    expect_eq_int(controller_record_after_keep.value("connection_limit", 0), 4, "account controller update record field");
    account_response = account_controller.update_account("ctrl-user", R"({"password":"rotated","state":1,"active":1})");
    expect_eq_int(account_response.status_code, 200, "account controller update rotates password ok");
    auto controller_record_after_rotate = account_controller_redis.hget(navcaster::redis_keys::ACT_RECORD, "ctrl-user");
    expect_true(controller_record_after_rotate.value("password_hash", std::string{}) != controller_record_after_keep.value("password_hash", std::string{}), "account controller password rotation changes hash");
    account_response = account_controller.update_account("ctrl-user", R"({"account":"other","password":"secret"})");
    expect_eq_int(account_response.status_code, 400, "account controller rejects account mismatch");
    account_response = account_controller.update_account("", R"({"password":"secret"})");
    expect_eq_int(account_response.status_code, 400, "account controller update missing account");
    account_response = account_controller.update_account("ctrl-user", "{");
    expect_eq_int(account_response.status_code, 400, "account controller update invalid json");
    account_response = account_controller.update_account("missing", R"({"password":"secret"})");
    expect_eq_int(account_response.status_code, 404, "account controller update missing account");
    account_response = account_controller.update_account("ctrl-user", R"({"password":"","state":2,"active":1})");
    expect_eq_int(account_response.status_code, 200, "account controller disables account ok");
    expect_true(account_controller_redis.hget(navcaster::redis_keys::ACT_ACTIVE, "ctrl-user").is_null(), "account controller disabled account clears active index");
    account_response = account_controller.delete_account("");
    expect_eq_int(account_response.status_code, 400, "account controller delete missing account");
    account_response = account_controller.delete_account("missing");
    expect_eq_int(account_response.status_code, 404, "account controller delete missing account not found");
    account_response = account_controller.delete_account("ctrl-user");
    expect_eq_int(account_response.status_code, 200, "account controller delete ok");
    expect_true(account_controller_redis.hget(navcaster::redis_keys::ACT_RECORD, "ctrl-user").is_null(), "account controller delete removes account");

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

    navcaster::storage::AccessRepositoryResult access_plan;
    nlohmann::json group_body = {{"group_uid", "survey"}, {"nearest_mpt_enable", true}};
    expect_true(navcaster::storage::build_access_group_plan(group_body, 4000, access_plan, &reason), "access group plan");
    expect_eq(access_plan.uid, "survey", "access group uid fallback");
    expect_eq(access_plan.record.value("uid", std::string{}), "survey", "access group record uid");
    expect_eq(access_plan.record.value("group_name", std::string{}), "survey", "access group default name");
    expect_true(access_plan.record.value("allow_access_inside_group", false), "access group allow access default");
    expect_true(access_plan.record.value("create_time", 0) == 4000, "access group create time");
    expect_true(!navcaster::storage::build_access_group_plan(nlohmann::json::object(), 4000, access_plan, &reason), "access group missing uid rejected");

    navcaster::storage::AccessRepository access_repo(fake_redis);
    access_repo.ensure_builtin_groups(4000);
    expect_true(fake_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "default").is_object(), "access repository ensures default group");
    expect_true(fake_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "SYSTEM").is_object(), "access repository ensures system group");
    auto access_result = access_repo.create_group(group_body, 4001);
    expect_true(access_result.status == navcaster::storage::RepositoryStatus::Ok, "access repository create group ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "survey").is_object(), "access repository writes group");
    expect_eq(fake_redis.publishes.back().second, "ACCESS", "access group create publishes");
    expect_true(access_repo.create_group(group_body, 4002).status == navcaster::storage::RepositoryStatus::Conflict, "access repository duplicate group");
    access_result = access_repo.update_group("survey", {{"group_name", "Surveyors"}}, 4003);
    expect_true(access_result.status == navcaster::storage::RepositoryStatus::Ok, "access repository update group ok");
    expect_eq(fake_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "survey").value("group_name", std::string{}), "Surveyors", "access repository update group name");
    expect_true(access_repo.delete_group("default").status == navcaster::storage::RepositoryStatus::Invalid, "access repository protects default group");

    nlohmann::json item_body = {{"mountpoint", "MOUNT9"}, {"allow_visible", 1}};
    expect_true(navcaster::storage::build_access_item_plan("survey", item_body, access_plan, &reason), "access item plan");
    expect_eq(access_plan.group_uid, "survey", "access item group uid");
    expect_eq(access_plan.mountpoint, "MOUNT9", "access item mountpoint alias");
    expect_eq(access_plan.record.value("mount_point_name", std::string{}), "MOUNT9", "access item normalized mountpoint");
    expect_eq_int(access_plan.record.value("allow_access", -1), 0, "access item default allow access");
    expect_true(!navcaster::storage::build_access_item_plan("", item_body, access_plan, &reason), "access item missing group rejected");
    expect_true(!navcaster::storage::build_access_item_plan("survey", nlohmann::json::object(), access_plan, &reason), "access item missing mount rejected");

    access_result = access_repo.create_item("survey", item_body);
    expect_true(access_result.status == navcaster::storage::RepositoryStatus::Ok, "access repository create item ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::access_item("survey").c_str(), "MOUNT9").is_object(), "access repository writes item bucket");
    expect_eq(fake_redis.publishes.back().second, "ACCESS", "access item create publishes");
    expect_true(access_repo.create_item("survey", item_body).status == navcaster::storage::RepositoryStatus::Conflict, "access repository duplicate item");
    access_result = access_repo.update_item("survey", {{"mount", "MOUNT9"}, {"allow_access", 1}});
    expect_true(access_result.status == navcaster::storage::RepositoryStatus::Ok, "access repository update item ok");
    expect_eq_int(fake_redis.hget(navcaster::redis_keys::access_item("survey").c_str(), "MOUNT9").value("allow_access", 0), 1, "access repository update item value");
    expect_true(access_repo.delete_item("survey", "MOUNT9").status == navcaster::storage::RepositoryStatus::Ok, "access repository delete item ok");
    expect_true(access_repo.delete_item("survey", "MOUNT9").status == navcaster::storage::RepositoryStatus::NotFound, "access repository missing item delete");

    navcaster::storage::RelayRepositoryResult relay_plan;
    nlohmann::json relay_body = {{"uid", "relay-1"}, {"login_mpt", "LOCAL"}};
    expect_true(navcaster::storage::build_relay_record_plan(navcaster::storage::RelayKind::Pull, relay_body, relay_plan, &reason), "relay plan");
    expect_eq(relay_plan.uid, "relay-1", "relay plan uid");
    expect_true(relay_plan.record.value("enabled", false), "relay plan enabled default");
    expect_true(!navcaster::storage::build_relay_record_plan(navcaster::storage::RelayKind::Pull, nlohmann::json::object(), relay_plan, &reason), "relay missing uid rejected");

    navcaster::storage::RelayRepository relay_repo(fake_redis);
    auto relay_result = relay_repo.create_record(navcaster::storage::RelayKind::Pull, relay_body);
    expect_true(relay_result.status == navcaster::storage::RepositoryStatus::Ok, "relay repository create pull ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::PULL_RECORD, "relay-1").is_object(), "relay repository writes pull record");
    expect_true(relay_repo.create_record(navcaster::storage::RelayKind::Pull, relay_body).status == navcaster::storage::RepositoryStatus::Conflict, "relay repository duplicate pull");
    fake_redis.hset(navcaster::redis_keys::PULL_STAT, "relay-1", nlohmann::json{{"state", 1}}.dump());
    relay_result = relay_repo.update_record(navcaster::storage::RelayKind::Pull, "relay-1", {{"target_ip", "127.0.0.1"}});
    expect_true(relay_result.status == navcaster::storage::RepositoryStatus::Ok, "relay repository update pull ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::PULL_STAT, "relay-1").is_null(), "relay repository update clears pull state");
    fake_redis.hset(navcaster::redis_keys::PULL_STAT, "relay-1", nlohmann::json{{"state", 1}}.dump());
    relay_result = relay_repo.set_enabled(navcaster::storage::RelayKind::Pull, "relay-1", false);
    expect_true(relay_result.status == navcaster::storage::RepositoryStatus::Ok, "relay repository stop pull ok");
    expect_true(!fake_redis.hget(navcaster::redis_keys::PULL_STAT, "relay-1").is_null(), "relay repository stop keeps pull state");
    expect_true(!fake_redis.hget(navcaster::redis_keys::PULL_RECORD, "relay-1").value("enabled", true), "relay repository stop disables record");
    relay_result = relay_repo.set_enabled(navcaster::storage::RelayKind::Pull, "relay-1", true);
    expect_true(relay_result.status == navcaster::storage::RepositoryStatus::Ok, "relay repository start pull ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::PULL_RECORD, "relay-1").value("enabled", false), "relay repository start enables record");
    relay_result = relay_repo.delete_record(navcaster::storage::RelayKind::Pull, "relay-1");
    expect_true(relay_result.status == navcaster::storage::RepositoryStatus::Ok, "relay repository delete pull ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::PULL_RECORD, "relay-1").is_null(), "relay repository delete removes pull record");
    expect_true(fake_redis.hget(navcaster::redis_keys::PULL_STAT, "relay-1").is_null(), "relay repository delete clears pull state");
    expect_true(relay_repo.delete_record(navcaster::storage::RelayKind::Pull, "relay-1").status == navcaster::storage::RepositoryStatus::NotFound, "relay repository missing pull delete");

    nlohmann::json push_body = {{"uid", "push-1"}, {"enabled", false}};
    relay_result = relay_repo.create_record(navcaster::storage::RelayKind::Push, push_body);
    expect_true(relay_result.status == navcaster::storage::RepositoryStatus::Ok, "relay repository create push ok");
    expect_true(!fake_redis.hget(navcaster::redis_keys::PUSH_RECORD, "push-1").value("enabled", true), "relay repository preserves push enabled");
    fake_redis.hset(navcaster::redis_keys::PUSH_STAT, "push-1", nlohmann::json{{"state", 1}}.dump());
    expect_true(relay_repo.update_record(navcaster::storage::RelayKind::Push, "push-1", {{"target_ip", "127.0.0.1"}}).status == navcaster::storage::RepositoryStatus::Ok, "relay repository update push ok");
    expect_true(fake_redis.hget(navcaster::redis_keys::PUSH_STAT, "push-1").is_null(), "relay repository update clears push state");

    expect_eq(navcaster::storage::runtime_state_key(navcaster::storage::RuntimeStateKind::Server), navcaster::redis_keys::MPT_STAT, "runtime server key");
    expect_eq(navcaster::storage::runtime_state_key(navcaster::storage::RuntimeStateKind::Client), navcaster::redis_keys::USR_STAT, "runtime client key");
    expect_eq(navcaster::storage::runtime_state_key(navcaster::storage::RuntimeStateKind::Stream), navcaster::redis_keys::STR_STAT, "runtime stream key");
    expect_eq(navcaster::storage::runtime_state_key(navcaster::storage::RuntimeStateKind::Node), navcaster::redis_keys::CASTER_NODE, "runtime node key");
    navcaster::storage::RuntimeStateRepository runtime_repo(fake_redis);
    fake_redis.hset(navcaster::redis_keys::MPT_STAT, "S1", nlohmann::json{{"uid", "S1"}}.dump());
    expect_true(runtime_repo.list(navcaster::storage::RuntimeStateKind::Server).contains("S1"), "runtime repository lists servers");
    expect_eq(runtime_repo.get(navcaster::storage::RuntimeStateKind::Server, "S1").value("uid", std::string{}), "S1", "runtime repository gets server");
    expect_true(runtime_repo.get(navcaster::storage::RuntimeStateKind::Server, "").is_null(), "runtime repository rejects empty get");

    FakeRedisHashClient runtime_controller_redis;
    navcaster::http_api::RuntimeStateController runtime_controller(runtime_controller_redis);
    runtime_controller_redis.hset(navcaster::redis_keys::MPT_STAT, "srv-ctrl", nlohmann::json{{"uid", "srv-ctrl"}}.dump());
    runtime_controller_redis.hset(navcaster::redis_keys::USR_STAT, "cli-ctrl", nlohmann::json{{"uid", "cli-ctrl"}}.dump());
    runtime_controller_redis.hset(navcaster::redis_keys::STR_STAT, "str-ctrl", nlohmann::json{{"uid", "str-ctrl"}}.dump());
    runtime_controller_redis.hset(navcaster::redis_keys::CASTER_NODE, "node-ctrl", nlohmann::json{{"uid", "node-ctrl"}}.dump());
    auto runtime_response = runtime_controller.list(navcaster::storage::RuntimeStateKind::Server);
    expect_eq_int(runtime_response.status_code, 200, "runtime controller list servers ok");
    auto runtime_controller_body = nlohmann::json::parse(runtime_response.body);
    expect_true(runtime_controller_body.contains("srv-ctrl"), "runtime controller list servers contains record");
    runtime_response = runtime_controller.get(navcaster::storage::RuntimeStateKind::Server, "srv-ctrl");
    expect_eq_int(runtime_response.status_code, 200, "runtime controller get server ok");
    runtime_response = runtime_controller.get(navcaster::storage::RuntimeStateKind::Server, "");
    expect_eq_int(runtime_response.status_code, 400, "runtime controller missing id");
    runtime_response = runtime_controller.get(navcaster::storage::RuntimeStateKind::Server, "missing");
    expect_eq_int(runtime_response.status_code, 404, "runtime controller missing record");
    runtime_response = runtime_controller.list(navcaster::storage::RuntimeStateKind::Client);
    runtime_controller_body = nlohmann::json::parse(runtime_response.body);
    expect_true(runtime_controller_body.contains("cli-ctrl"), "runtime controller list clients contains record");
    runtime_response = runtime_controller.list(navcaster::storage::RuntimeStateKind::Stream);
    runtime_controller_body = nlohmann::json::parse(runtime_response.body);
    expect_true(runtime_controller_body.contains("str-ctrl"), "runtime controller list streams contains record");
    runtime_response = runtime_controller.list(navcaster::storage::RuntimeStateKind::Node);
    runtime_controller_body = nlohmann::json::parse(runtime_response.body);
    expect_true(runtime_controller_body.contains("node-ctrl"), "runtime controller list nodes contains record");
    runtime_response = runtime_controller.get(navcaster::storage::RuntimeStateKind::Client, "cli-ctrl");
    expect_eq_int(runtime_response.status_code, 200, "runtime controller get client ok");
    runtime_response = runtime_controller.get(navcaster::storage::RuntimeStateKind::Stream, "str-ctrl");
    expect_eq_int(runtime_response.status_code, 200, "runtime controller get stream ok");
    runtime_response = runtime_controller.get(navcaster::storage::RuntimeStateKind::Node, "node-ctrl");
    expect_eq_int(runtime_response.status_code, 200, "runtime controller get node ok");

    FakeRedisHashClient runtime_command_redis;
    navcaster::http_api::RuntimeCommandService runtime_commands(runtime_command_redis);
    auto runtime_command_response = runtime_commands.kick(navcaster::storage::RuntimeStateKind::Server, "");
    expect_eq_int(runtime_command_response.status_code, 400, "runtime command missing uid");
    runtime_command_response = runtime_commands.kick(navcaster::storage::RuntimeStateKind::Stream, "str-ctrl");
    expect_eq_int(runtime_command_response.status_code, 400, "runtime command rejects stream kick");
    runtime_command_response = runtime_commands.kick(navcaster::storage::RuntimeStateKind::Server, "missing");
    expect_eq_int(runtime_command_response.status_code, 404, "runtime command missing server");
    runtime_command_redis.hset(navcaster::redis_keys::MPT_STAT, "srv-kick", nlohmann::json{{"uid", "srv-kick"}}.dump());
    runtime_command_response = runtime_commands.kick(navcaster::storage::RuntimeStateKind::Server, "srv-kick");
    expect_eq_int(runtime_command_response.status_code, 200, "runtime command kick server ok");
    expect_eq(runtime_command_redis.publishes.back().first, navcaster::redis_keys::CASTER_BROADCAST, "runtime command publishes caster broadcast");
    broadcast_msg server_kick;
    expect_eq_int(server_kick.fromString(runtime_command_redis.publishes.back().second), 0, "runtime command server broadcast parses");
    expect_true(server_kick.type == caster::core::BOARDCAST_TYPE_SERVER_OPERATE, "runtime command server broadcast type");
    expect_true(server_kick.operate == caster::core::BOARDCAST_OPERATE_DELETE, "runtime command server broadcast operate");
    expect_eq(server_kick.target, "srv-kick", "runtime command server broadcast target");
    runtime_command_redis.hset(navcaster::redis_keys::USR_STAT, "cli-kick", nlohmann::json{{"uid", "cli-kick"}}.dump());
    runtime_command_response = runtime_commands.kick(navcaster::storage::RuntimeStateKind::Client, "cli-kick");
    expect_eq_int(runtime_command_response.status_code, 200, "runtime command kick client ok");
    broadcast_msg client_kick;
    expect_eq_int(client_kick.fromString(runtime_command_redis.publishes.back().second), 0, "runtime command client broadcast parses");
    expect_true(client_kick.type == caster::core::BOARDCAST_TYPE_CLIENT_OPERATE, "runtime command client broadcast type");
    expect_eq(client_kick.target, "cli-kick", "runtime command client broadcast target");
    runtime_command_redis.publish_ok = false;
    runtime_command_response = runtime_commands.kick(navcaster::storage::RuntimeStateKind::Client, "cli-kick");
    expect_eq_int(runtime_command_response.status_code, 500, "runtime command publish failure");
    runtime_command_redis.publish_ok = true;

    const std::string sourcetable_text =
        "SOURCETABLE 200 OK\r\n"
        "STR;MOUNT1;Identifier;RTCM 3.3;1004(1),1005(10);2;GPS;NET;USA;39.123;-104.456;0;0;Caster;none;B;N;0;misc\r\n"
        "STR;MOUNT2;Other;RTCM 3.2;;;;;CAN;45.000;-75.000\r\n"
        "ENDSOURCETABLE\r\n"
        "STR;IGNORED;AfterEnd;RTCM\r\n";
    auto sourcetable_mounts = navcaster::http_api::parse_sourcetable_text(sourcetable_text);
    expect_eq_int(static_cast<int>(sourcetable_mounts.size()), 2, "sourcetable parser stops at end");
    expect_eq(sourcetable_mounts[0].value("mountpoint", std::string{}), "MOUNT1", "sourcetable parser mountpoint");
    expect_eq(sourcetable_mounts[0].value("identifier", std::string{}), "Identifier", "sourcetable parser identifier");
    expect_eq(sourcetable_mounts[0].value("format", std::string{}), "RTCM 3.3", "sourcetable parser format");
    expect_eq(sourcetable_mounts[0].value("format_details", std::string{}), "1004(1),1005(10)", "sourcetable parser format details");
    expect_eq(sourcetable_mounts[0].value("country", std::string{}), "USA", "sourcetable parser country");
    expect_eq(sourcetable_mounts[0].value("latitude", std::string{}), "39.123", "sourcetable parser latitude");
    expect_eq(sourcetable_mounts[0].value("longitude", std::string{}), "-104.456", "sourcetable parser longitude");

    navcaster::http_api::SourcetableRequest sourcetable_request;
    sourcetable_request.host = "caster.example";
    sourcetable_request.port = 2102;
    sourcetable_request.username = "user";
    sourcetable_request.password = "pass";
    sourcetable_request.ntrip_version = "1.0";
    auto request_10 = navcaster::http_api::build_sourcetable_request(sourcetable_request);
    expect_true(request_10.find("GET / HTTP/1.0\r\n") == 0, "sourcetable request ntrip 1.0 line");
    expect_true(request_10.find("Host: caster.example:2102\r\n") != std::string::npos, "sourcetable request host header");
    expect_true(request_10.find("Authorization: Basic dXNlcjpwYXNz\r\n") != std::string::npos, "sourcetable request basic auth");
    sourcetable_request.ntrip_version = "2.0";
    auto request_20 = navcaster::http_api::build_sourcetable_request(sourcetable_request);
    expect_true(request_20.find("GET / HTTP/1.1\r\n") == 0, "sourcetable request ntrip 2.0 line");
    expect_true(request_20.find("Ntrip-Version: Ntrip/2.0\r\n") != std::string::npos, "sourcetable request ntrip 2.0 header");
    expect_true(request_20.find("Connection: close\r\n") != std::string::npos, "sourcetable request close header");

    std::string captured_request;
    navcaster::http_api::SourcetableService sourcetable_service(
        [&](const navcaster::http_api::SourcetableRequest &, const std::string &request_text) {
            captured_request = request_text;
            return navcaster::http_api::SourcetableFetchResult{true, sourcetable_text, {}, {}};
        });
    auto sourcetable_response = sourcetable_service.fetch_remote(R"({"host":"caster.example","port":2102,"username":"user","password":"pass","ntrip_version":"2.0"})");
    expect_eq_int(sourcetable_response.status_code, 200, "sourcetable service fetch ok");
    expect_true(captured_request.find("Authorization: Basic dXNlcjpwYXNz\r\n") != std::string::npos, "sourcetable service passes request");
    auto sourcetable_body = nlohmann::json::parse(sourcetable_response.body);
    expect_eq_int(static_cast<int>(sourcetable_body["mountpoints"].size()), 2, "sourcetable service response mountpoints");
    sourcetable_response = sourcetable_service.fetch_remote("{");
    expect_eq_int(sourcetable_response.status_code, 400, "sourcetable service invalid json");
    sourcetable_response = sourcetable_service.fetch_remote(R"({})");
    expect_eq_int(sourcetable_response.status_code, 400, "sourcetable service missing host");
    navcaster::http_api::SourcetableService sourcetable_error_service(
        [](const navcaster::http_api::SourcetableRequest &, const std::string &) {
            return navcaster::http_api::SourcetableFetchResult{false, {}, "DNS resolve failed", "no such host"};
        });
    sourcetable_response = sourcetable_error_service.fetch_remote(R"({"host":"missing.example"})");
    expect_eq_int(sourcetable_response.status_code, 502, "sourcetable service fetch error");
    sourcetable_body = nlohmann::json::parse(sourcetable_response.body);
    expect_eq(sourcetable_body.value("detail", std::string{}), "no such host", "sourcetable service fetch error detail");
    sourcetable_response = sourcetable_service.local_from_text(sourcetable_text);
    expect_eq_int(sourcetable_response.status_code, 200, "sourcetable service local ok");

    navcaster::http_api::StatisticsService statistics_service;
    const long long stats_start = 1000;
    const long long stats_end = 8200;
    const long long stats_now = 7000;
    nlohmann::json mpt_logs = {
        {"srv1", {{"type", 1}, {"name", "MOUNT_A"}, {"connect_time", 1000}, {"disconnect_time", 4600}}},
        {"srv2", {{"type", 1}, {"name", "MOUNT_A"}, {"connect_time", 2800}, {"disconnect_time", 0}}},
        {"pull1", {{"type", 5}, {"name", "PULL_A"}, {"connect_time", 1000}, {"disconnect_time", 2000}}},
        {"outside", {{"type", 1}, {"name", "OUT"}, {"connect_time", 9000}, {"disconnect_time", 9200}}},
        {"bad", "ignored"}
    };
    nlohmann::json usr_logs = {
        {"usr1", {{"type", 2}, {"name", "user1"}, {"mount", "MOUNT_A"}, {"connect_time", 1000}, {"disconnect_time", 2800}}},
        {"push1", {{"type", 6}, {"name", "push1"}, {"mount", "MOUNT_A"}, {"connect_time", 1000}, {"disconnect_time", 0}}},
        {"usr2", {{"type", 3}, {"name", "user2"}, {"mount", "MOUNT_B"}, {"connect_time", 3000}, {"disconnect_time", 8200}}}
    };

    auto stats_overview = statistics_service.overview(mpt_logs, usr_logs, stats_start, stats_end, stats_now);
    expect_eq_int(stats_overview.value("mpt_connections", 0), 2, "statistics overview mpt connections");
    expect_eq_int(stats_overview.value("usr_connections", 0), 2, "statistics overview usr connections");
    expect_eq_int(stats_overview.value("pull_connections", 0), 1, "statistics overview pull connections");
    expect_eq_int(stats_overview.value("push_connections", 0), 1, "statistics overview push connections");
    expect_eq_int(stats_overview.value("peak_concurrent_mpt", 0), 2, "statistics overview peak mpt");
    expect_eq_int(stats_overview.value("peak_concurrent_usr", 0), 2, "statistics overview peak usr");
    expect_eq_int(stats_overview.value("peak_concurrent_pull", 0), 1, "statistics overview peak pull");
    expect_eq_int(stats_overview.value("peak_concurrent_push", 0), 1, "statistics overview peak push");
    expect_eq_int(stats_overview.value("avg_duration_mpt", 0), 3900, "statistics overview avg mpt duration");
    expect_eq_int(stats_overview.value("avg_duration_usr", 0), 3500, "statistics overview avg usr duration");
    expect_eq_int(stats_overview.value("unique_mountpoints", 0), 1, "statistics overview unique mounts");
    expect_eq_int(stats_overview.value("unique_users", 0), 2, "statistics overview unique users");
    expect_eq_int(stats_overview.value("bucket_seconds", 0), 3600, "statistics overview bucket seconds");
    expect_eq_int(static_cast<int>(stats_overview["hourly_trend"].size()), 2, "statistics overview trend buckets");
    expect_eq_int(stats_overview["hourly_trend"][0].value("mpt", 0), 2, "statistics overview first mpt bucket");
    expect_eq_int(stats_overview["hourly_trend"][0].value("usr", 0), 2, "statistics overview first usr bucket");
    expect_eq_int(stats_overview["hourly_trend"][1].value("push", 0), 1, "statistics overview second push bucket");

    auto stats_daily = statistics_service.daily("1970-01-01", mpt_logs, usr_logs, stats_start, stats_start + 86400, stats_now);
    expect_eq(stats_daily.value("date", std::string{}), "1970-01-01", "statistics daily date");
    expect_eq_int(static_cast<int>(stats_daily["hourly_trend"].size()), 24, "statistics daily trend buckets");
    expect_true(!stats_daily.contains("bucket_seconds"), "statistics daily omits bucket seconds");
    expect_eq_int(stats_daily.value("mpt_connections", 0), 3, "statistics daily mpt connections");

    auto mpt_ranking = statistics_service.mountpoint_ranking(mpt_logs, stats_start, stats_end, stats_now, 20);
    expect_eq_int(static_cast<int>(mpt_ranking.size()), 2, "statistics mount ranking size");
    expect_eq(mpt_ranking[0].value("name", std::string{}), "MOUNT_A", "statistics mount ranking first");
    expect_eq_int(mpt_ranking[0].value("total_duration", 0), 7800, "statistics mount ranking total duration");
    expect_eq_int(mpt_ranking[0].value("connections", 0), 2, "statistics mount ranking connections");
    expect_eq_int(mpt_ranking[0].value("last_seen", 0), 7000, "statistics mount ranking last seen");
    expect_eq(mpt_ranking[0]["types"][0].get<std::string>(), "SERVER", "statistics mount ranking server type");
    expect_eq(mpt_ranking[1]["types"][0].get<std::string>(), "PULL", "statistics mount ranking pull type");

    auto usr_ranking = statistics_service.user_ranking(usr_logs, stats_start, stats_end, stats_now, 2);
    expect_eq_int(static_cast<int>(usr_ranking.size()), 2, "statistics user ranking limit");
    expect_eq(usr_ranking[0].value("name", std::string{}), "push1", "statistics user ranking first");
    expect_eq_int(usr_ranking[0].value("total_duration", 0), 6000, "statistics user ranking push duration");
    expect_eq_int(usr_ranking[0].value("mount_count", 0), 1, "statistics user ranking mount count");
    expect_eq(usr_ranking[0]["types"][0].get<std::string>(), "PUSH", "statistics user ranking push type");
    expect_eq(usr_ranking[1]["types"][0].get<std::string>(), "NEAREST", "statistics user ranking nearest type");

    navcaster::storage::ConfigSection config_section;
    expect_true(navcaster::storage::parse_config_section("service", config_section), "config parses service section");
    expect_true(config_section == navcaster::storage::ConfigSection::Service, "config service enum");
    expect_true(navcaster::storage::parse_config_section("core", config_section), "config parses core section");
    expect_true(config_section == navcaster::storage::ConfigSection::Core, "config core enum");
    expect_true(navcaster::storage::parse_config_section("auth", config_section), "config parses auth section");
    expect_true(config_section == navcaster::storage::ConfigSection::Auth, "config auth enum");
    expect_true(!navcaster::storage::parse_config_section("missing", config_section), "config rejects unknown section");
    expect_eq(navcaster::storage::config_section_key(navcaster::storage::ConfigSection::Service), navcaster::redis_keys::CONF_SERVICE, "config service key");
    expect_eq(navcaster::storage::config_section_key(navcaster::storage::ConfigSection::Core), navcaster::redis_keys::CONF_CORE, "config core key");
    expect_eq(navcaster::storage::config_section_key(navcaster::storage::ConfigSection::Auth), navcaster::redis_keys::CONF_AUTH, "config auth key");
    navcaster::storage::ConfigRepository config_repo(fake_redis);
    auto config_result = config_repo.update_config(navcaster::storage::ConfigSection::Service, {{"port", 8080}});
    expect_true(config_result.status == navcaster::storage::RepositoryStatus::Ok, "config repository update ok");
    expect_eq_int(config_repo.get_config(navcaster::storage::ConfigSection::Service).value("port", 0), 8080, "config repository get service");
    expect_true(config_repo.list_configs().contains("service"), "config repository lists service config");
    expect_true(config_repo.save_config(navcaster::storage::ConfigSection::Auth, R"({"admin_user":"root"})"), "config repository save raw json");
    expect_eq(config_repo.get_config(navcaster::storage::ConfigSection::Auth).value("admin_user", std::string{}), "root", "config repository save raw value");
    fake_redis.set_ok = false;
    config_result = config_repo.update_config(navcaster::storage::ConfigSection::Core, {{"worker", 2}});
    expect_true(config_result.status == navcaster::storage::RepositoryStatus::RedisError, "config repository reports set failure");
    fake_redis.set_ok = true;

    FakeRedisHashClient config_controller_redis;
    navcaster::storage::ConfigRepository config_controller_repo(config_controller_redis);
    navcaster::http_api::ConfigController config_controller(config_controller_redis, {"admin", "admin-pass"});
    auto config_response = config_controller.get_config("auth");
    expect_eq_int(config_response.status_code, 200, "config controller auth defaults ok");
    auto config_body = nlohmann::json::parse(config_response.body);
    expect_eq(config_body.value("admin_user", std::string{}), "admin", "config controller auth default user");
    expect_missing(config_body, "admin_password", "config controller hides auth password");
    config_response = config_controller.get_config("missing");
    expect_eq_int(config_response.status_code, 404, "config controller unknown get");
    config_response = config_controller.update_config("service", R"({"port":9000})");
    expect_eq_int(config_response.status_code, 200, "config controller updates service");
    expect_eq_int(config_controller_repo.get_config(navcaster::storage::ConfigSection::Service).value("port", 0), 9000, "config controller wrote service");
    config_response = config_controller.update_config("auth", R"({"admin_user":"ops"})");
    expect_eq_int(config_response.status_code, 400, "config controller requires old password");
    config_response = config_controller.update_config("auth", R"({"old_password":"wrong","admin_user":"ops"})");
    expect_eq_int(config_response.status_code, 403, "config controller rejects wrong old password");
    config_response = config_controller.update_config("auth", R"({"old_password":"admin-pass","admin_user":"ops","admin_password":"new-pass"})");
    expect_eq_int(config_response.status_code, 200, "config controller updates auth");
    auto saved_auth = config_controller_repo.get_config(navcaster::storage::ConfigSection::Auth);
    expect_eq(saved_auth.value("admin_user", std::string{}), "ops", "config controller saved auth user");
    expect_eq(saved_auth.value("admin_password", std::string{}), "new-pass", "config controller saved auth password");
    expect_missing(saved_auth, "old_password", "config controller strips old password");
    config_response = config_controller.update_config("service", "{");
    expect_eq_int(config_response.status_code, 400, "config controller rejects invalid json");

    FakeRedisHashClient source_controller_redis;
    navcaster::http_api::SourceController source_controller(source_controller_redis, 9010);
    auto source_response = source_controller.list_sources();
    expect_eq_int(source_response.status_code, 200, "source controller list ok");
    source_response = source_controller.create_source(R"({"mountpoint":"CTRL1"})");
    expect_eq_int(source_response.status_code, 201, "source controller create ok");
    auto source_controller_body = nlohmann::json::parse(source_response.body);
    expect_eq(source_controller_body.value("mountpoint", std::string{}), "CTRL1", "source controller create response");
    source_response = source_controller.get_source("CTRL1");
    expect_eq_int(source_response.status_code, 200, "source controller get ok");
    source_controller_body = nlohmann::json::parse(source_response.body);
    expect_eq_int(source_controller_body.value("create_time", 0), 9010, "source controller create time");
    source_response = source_controller.create_source(R"({"mountpoint":"CTRL1"})");
    expect_eq_int(source_response.status_code, 409, "source controller duplicate create");
    source_response = source_controller.create_source("{");
    expect_eq_int(source_response.status_code, 400, "source controller invalid json");
    source_response = source_controller.get_source("");
    expect_eq_int(source_response.status_code, 400, "source controller missing get id");
    source_response = source_controller.get_source("missing");
    expect_eq_int(source_response.status_code, 404, "source controller missing get");
    source_response = source_controller.update_source("CTRL1", R"({"country":"CN"})");
    expect_eq_int(source_response.status_code, 200, "source controller update ok");
    source_response = source_controller.delete_source("CTRL1");
    expect_eq_int(source_response.status_code, 200, "source controller delete ok");
    source_response = source_controller.delete_source("CTRL1");
    expect_eq_int(source_response.status_code, 404, "source controller missing delete");

    FakeRedisHashClient caster_sse_redis;
    FakeRedisHashClient auth_sse_redis;
    navcaster::http_api::SseSnapshotService sse_snapshots(caster_sse_redis, auth_sse_redis);
    caster_sse_redis.hset(navcaster::redis_keys::MPT_STAT, "srv-1", nlohmann::json{{"uid", "srv-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::USR_STAT, "cli-1", nlohmann::json{{"uid", "cli-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::STR_STAT, "str-1", nlohmann::json{{"uid", "str-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::CASTER_NODE, "node-1", nlohmann::json{{"uid", "node-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::MPT_RECORD, "SRC1", nlohmann::json{{"mountpoint", "SRC1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::ALIAS_RULE, "AL1", nlohmann::json{{"uid", "AL1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::ACCESS_GROUP, "default", nlohmann::json{{"uid", "default"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::PULL_RECORD, "pull-1", nlohmann::json{{"uid", "pull-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::PULL_STAT, "pull-1", nlohmann::json{{"state", 1}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::PUSH_RECORD, "push-1", nlohmann::json{{"uid", "push-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::PUSH_STAT, "push-1", nlohmann::json{{"state", 1}}.dump());
    auth_sse_redis.hset(navcaster::redis_keys::ACT_RECORD, "acct-1", nlohmann::json{{"account", "acct-1"}}.dump());
    auth_sse_redis.hset(navcaster::redis_keys::STR_ACTIVE_LEGACY, "acct-1", nlohmann::json{{"account", "acct-1"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::ACT_RECORD, "wrong-redis", nlohmann::json{{"account", "wrong-redis"}}.dump());
    expect_true(sse_snapshots.servers().contains("srv-1"), "sse snapshot servers via runtime repo");
    expect_true(sse_snapshots.clients().contains("cli-1"), "sse snapshot clients via runtime repo");
    expect_true(sse_snapshots.streams().contains("str-1"), "sse snapshot streams via runtime repo");
    expect_true(sse_snapshots.nodes().contains("node-1"), "sse snapshot nodes via runtime repo");
    expect_true(sse_snapshots.sources().contains("SRC1"), "sse snapshot sources via repository");
    expect_true(sse_snapshots.aliases().contains("AL1"), "sse snapshot aliases via repository");
    expect_true(sse_snapshots.access_groups().contains("default"), "sse snapshot access groups via repository");
    expect_true(sse_snapshots.pull_records().contains("pull-1"), "sse snapshot pull records via repository");
    expect_true(sse_snapshots.pull_states().contains("pull-1"), "sse snapshot pull states via repository");
    expect_true(sse_snapshots.push_records().contains("push-1"), "sse snapshot push records via repository");
    expect_true(sse_snapshots.push_states().contains("push-1"), "sse snapshot push states via repository");
    expect_true(sse_snapshots.accounts().contains("acct-1"), "sse snapshot accounts uses auth redis");
    expect_true(!sse_snapshots.accounts().contains("wrong-redis"), "sse snapshot accounts ignores caster redis");
    expect_true(sse_snapshots.account_actives().contains("acct-1"), "sse snapshot account actives uses auth redis");

    FakeRedisHashClient alias_controller_redis;
    navcaster::http_api::AliasController alias_controller(alias_controller_redis, 10020);
    auto alias_response = alias_controller.list_aliases();
    expect_eq_int(alias_response.status_code, 200, "alias controller list ok");
    alias_response = alias_controller.create_alias(R"({"uid":"ALCTRL","source_name":"SRC1"})");
    expect_eq_int(alias_response.status_code, 201, "alias controller create ok");
    auto alias_controller_body = nlohmann::json::parse(alias_response.body);
    expect_eq(alias_controller_body.value("alias", std::string{}), "ALCTRL", "alias controller create response");
    expect_eq(alias_controller_redis.publishes.back().second, "ALIAS", "alias controller create publishes");
    alias_response = alias_controller.get_alias("ALCTRL");
    expect_eq_int(alias_response.status_code, 200, "alias controller get ok");
    alias_response = alias_controller.create_alias(R"({"uid":"ALCTRL","source_name":"SRC1"})");
    expect_eq_int(alias_response.status_code, 409, "alias controller duplicate create");
    alias_response = alias_controller.create_alias("{");
    expect_eq_int(alias_response.status_code, 400, "alias controller invalid json");
    alias_response = alias_controller.create_alias(R"({"uid":"BAD"})");
    expect_eq_int(alias_response.status_code, 400, "alias controller missing source");
    alias_response = alias_controller.get_alias("");
    expect_eq_int(alias_response.status_code, 400, "alias controller missing get id");
    alias_response = alias_controller.get_alias("missing");
    expect_eq_int(alias_response.status_code, 404, "alias controller missing get");
    alias_response = alias_controller.update_alias("ALCTRL", R"({"source_name":"SRC2"})");
    expect_eq_int(alias_response.status_code, 200, "alias controller update ok");
    alias_response = alias_controller.delete_alias("ALCTRL");
    expect_eq_int(alias_response.status_code, 200, "alias controller delete ok");
    alias_response = alias_controller.delete_alias("ALCTRL");
    expect_eq_int(alias_response.status_code, 404, "alias controller missing delete");

    FakeRedisHashClient access_controller_redis;
    navcaster::http_api::AccessController access_controller(access_controller_redis, 11000);
    auto access_response = access_controller.list_groups();
    expect_eq_int(access_response.status_code, 200, "access controller list groups ok");
    access_response = access_controller.create_group(R"({"uid":"ops","group_name":"Ops"})");
    expect_eq_int(access_response.status_code, 201, "access controller create group ok");
    auto access_controller_body = nlohmann::json::parse(access_response.body);
    expect_eq(access_controller_body.value("uid", std::string{}), "ops", "access controller create group response");
    expect_eq_int(access_controller_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "ops").value("create_time", 0), 11000, "access controller create group timestamp");
    expect_eq(access_controller_redis.publishes.back().second, "ACCESS", "access controller create group publishes");
    access_response = access_controller.get_group("ops");
    expect_eq_int(access_response.status_code, 200, "access controller get group ok");
    access_response = access_controller.create_group(R"({"uid":"ops"})");
    expect_eq_int(access_response.status_code, 409, "access controller duplicate group");
    access_response = access_controller.create_group("{");
    expect_eq_int(access_response.status_code, 400, "access controller group invalid json");
    access_response = access_controller.create_group(R"({})");
    expect_eq_int(access_response.status_code, 400, "access controller group missing uid");
    access_response = access_controller.get_group("");
    expect_eq_int(access_response.status_code, 400, "access controller missing group id");
    access_response = access_controller.get_group("missing");
    expect_eq_int(access_response.status_code, 404, "access controller missing group");
    access_response = access_controller.update_group("ops", R"({"allow_access_inside_group":false})");
    expect_eq_int(access_response.status_code, 200, "access controller update group ok");
    expect_true(!access_controller_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "ops").value("allow_access_inside_group", true), "access controller update group value");
    access_response = access_controller.update_group("", R"({"group_name":"bad"})");
    expect_eq_int(access_response.status_code, 400, "access controller missing update group id");
    access_response = access_controller.update_group("ops", "{");
    expect_eq_int(access_response.status_code, 400, "access controller update group invalid json");
    access_response = access_controller.delete_group("default");
    expect_eq_int(access_response.status_code, 403, "access controller protects default group");
    access_response = access_controller.delete_group("missing");
    expect_eq_int(access_response.status_code, 404, "access controller missing group delete");

    access_response = access_controller.list_items("");
    expect_eq_int(access_response.status_code, 400, "access controller missing item group list");
    access_response = access_controller.list_items("ops");
    expect_eq_int(access_response.status_code, 200, "access controller list items ok");
    access_response = access_controller.create_item("ops", R"({"mountpoint":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 201, "access controller create item ok");
    expect_eq(access_controller_redis.publishes.back().second, "ACCESS", "access controller create item publishes");
    expect_true(access_controller_redis.hget(navcaster::redis_keys::access_item("ops").c_str(), "MPTCTRL").is_object(), "access controller writes item");
    access_response = access_controller.create_item("ops", R"({"mountpoint":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 409, "access controller duplicate item");
    access_response = access_controller.create_item("", R"({"mountpoint":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 400, "access controller missing item group create");
    access_response = access_controller.create_item("ops", "{");
    expect_eq_int(access_response.status_code, 400, "access controller create item invalid json");
    access_response = access_controller.create_item("ops", R"({})");
    expect_eq_int(access_response.status_code, 400, "access controller create item missing mount");
    access_response = access_controller.update_item("ops", R"({"mount":"MPTCTRL","allow_access":1})");
    expect_eq_int(access_response.status_code, 200, "access controller update item ok");
    expect_eq_int(access_controller_redis.hget(navcaster::redis_keys::access_item("ops").c_str(), "MPTCTRL").value("allow_access", 0), 1, "access controller update item value");
    access_response = access_controller.update_item("", R"({"group_uid":"ops","uid":"MPTCTRL","allow_visible":1})");
    expect_eq_int(access_response.status_code, 200, "access controller update item body group");
    access_response = access_controller.update_item("ops", "{");
    expect_eq_int(access_response.status_code, 400, "access controller update item invalid json");
    access_response = access_controller.update_item("", R"({"mount":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 400, "access controller update item missing group");
    access_response = access_controller.delete_item("ops", R"({"uid":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 200, "access controller delete item ok");
    access_response = access_controller.delete_item("ops", R"({"uid":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 404, "access controller missing item delete");
    access_response = access_controller.delete_item("", "{");
    expect_eq_int(access_response.status_code, 400, "access controller delete item invalid json");
    access_response = access_controller.delete_item("", R"({"uid":"MPTCTRL"})");
    expect_eq_int(access_response.status_code, 400, "access controller delete item missing group");
    access_response = access_controller.delete_group("ops");
    expect_eq_int(access_response.status_code, 200, "access controller delete group ok");

    FakeRedisHashClient relay_controller_redis;
    navcaster::http_api::RelayController relay_controller(relay_controller_redis);
    auto relay_response = relay_controller.list_records(navcaster::storage::RelayKind::Pull);
    expect_eq_int(relay_response.status_code, 200, "relay controller list pull ok");
    relay_response = relay_controller.create_record(navcaster::storage::RelayKind::Pull, R"({"uid":"pull-ctrl","login_mpt":"LOCAL"})");
    expect_eq_int(relay_response.status_code, 201, "relay controller create pull ok");
    auto relay_controller_body = nlohmann::json::parse(relay_response.body);
    expect_eq(relay_controller_body.value("uid", std::string{}), "pull-ctrl", "relay controller create response");
    expect_true(relay_controller_redis.hget(navcaster::redis_keys::PULL_RECORD, "pull-ctrl").value("enabled", false), "relay controller pull enabled default");
    relay_response = relay_controller.get_record(navcaster::storage::RelayKind::Pull, "pull-ctrl");
    expect_eq_int(relay_response.status_code, 200, "relay controller get pull ok");
    relay_response = relay_controller.create_record(navcaster::storage::RelayKind::Pull, R"({"uid":"pull-ctrl"})");
    expect_eq_int(relay_response.status_code, 409, "relay controller duplicate pull");
    relay_response = relay_controller.create_record(navcaster::storage::RelayKind::Pull, "{");
    expect_eq_int(relay_response.status_code, 400, "relay controller create invalid json");
    relay_response = relay_controller.create_record(navcaster::storage::RelayKind::Pull, R"({})");
    expect_eq_int(relay_response.status_code, 400, "relay controller create missing uid");
    relay_response = relay_controller.get_record(navcaster::storage::RelayKind::Pull, "");
    expect_eq_int(relay_response.status_code, 400, "relay controller missing get id");
    relay_response = relay_controller.get_record(navcaster::storage::RelayKind::Pull, "missing");
    expect_eq_int(relay_response.status_code, 404, "relay controller missing get");
    relay_controller_redis.hset(navcaster::redis_keys::PULL_STAT, "pull-ctrl", nlohmann::json{{"state", 1}}.dump());
    relay_response = relay_controller.update_record(navcaster::storage::RelayKind::Pull, "pull-ctrl", R"({"target_ip":"127.0.0.1"})");
    expect_eq_int(relay_response.status_code, 200, "relay controller update pull ok");
    expect_true(relay_controller_redis.hget(navcaster::redis_keys::PULL_STAT, "pull-ctrl").is_null(), "relay controller update clears state");
    relay_response = relay_controller.update_record(navcaster::storage::RelayKind::Pull, "", R"({"target_ip":"127.0.0.1"})");
    expect_eq_int(relay_response.status_code, 400, "relay controller update missing id");
    relay_response = relay_controller.update_record(navcaster::storage::RelayKind::Pull, "pull-ctrl", "{");
    expect_eq_int(relay_response.status_code, 400, "relay controller update invalid json");
    relay_controller_redis.hset(navcaster::redis_keys::PULL_STAT, "pull-ctrl", nlohmann::json{{"state", 1}}.dump());
    relay_response = relay_controller.set_enabled(navcaster::storage::RelayKind::Pull, "pull-ctrl", false);
    expect_eq_int(relay_response.status_code, 200, "relay controller stop pull ok");
    expect_true(!relay_controller_redis.hget(navcaster::redis_keys::PULL_RECORD, "pull-ctrl").value("enabled", true), "relay controller stop disables");
    expect_true(!relay_controller_redis.hget(navcaster::redis_keys::PULL_STAT, "pull-ctrl").is_null(), "relay controller stop keeps state");
    relay_response = relay_controller.set_enabled(navcaster::storage::RelayKind::Pull, "pull-ctrl", true);
    expect_eq_int(relay_response.status_code, 200, "relay controller start pull ok");
    relay_controller_body = nlohmann::json::parse(relay_response.body);
    expect_eq(relay_controller_body.value("uid", std::string{}), "pull-ctrl", "relay controller start response uid");
    expect_true(relay_controller_redis.hget(navcaster::redis_keys::PULL_RECORD, "pull-ctrl").value("enabled", false), "relay controller start enables");
    relay_response = relay_controller.set_enabled(navcaster::storage::RelayKind::Pull, "", true);
    expect_eq_int(relay_response.status_code, 400, "relay controller start missing id");
    relay_response = relay_controller.set_enabled(navcaster::storage::RelayKind::Pull, "missing", true);
    expect_eq_int(relay_response.status_code, 404, "relay controller start missing record");
    relay_response = relay_controller.list_states(navcaster::storage::RelayKind::Pull);
    expect_eq_int(relay_response.status_code, 200, "relay controller list pull states ok");
    relay_controller_body = nlohmann::json::parse(relay_response.body);
    expect_true(relay_controller_body.contains("pull-ctrl"), "relay controller state list contains pull");
    relay_response = relay_controller.delete_record(navcaster::storage::RelayKind::Pull, "pull-ctrl");
    expect_eq_int(relay_response.status_code, 200, "relay controller delete pull ok");
    expect_true(relay_controller_redis.hget(navcaster::redis_keys::PULL_RECORD, "pull-ctrl").is_null(), "relay controller delete removes record");
    expect_true(relay_controller_redis.hget(navcaster::redis_keys::PULL_STAT, "pull-ctrl").is_null(), "relay controller delete clears state");
    relay_response = relay_controller.delete_record(navcaster::storage::RelayKind::Pull, "pull-ctrl");
    expect_eq_int(relay_response.status_code, 404, "relay controller missing delete");

    relay_response = relay_controller.create_record(navcaster::storage::RelayKind::Push, R"({"uid":"push-ctrl","enabled":false})");
    expect_eq_int(relay_response.status_code, 201, "relay controller create push ok");
    expect_true(!relay_controller_redis.hget(navcaster::redis_keys::PUSH_RECORD, "push-ctrl").value("enabled", true), "relay controller preserves push enabled");
    relay_response = relay_controller.get_record(navcaster::storage::RelayKind::Push, "push-ctrl");
    expect_eq_int(relay_response.status_code, 200, "relay controller get push ok");
    relay_controller_redis.hset(navcaster::redis_keys::PUSH_STAT, "push-ctrl", nlohmann::json{{"state", 1}}.dump());
    relay_response = relay_controller.update_record(navcaster::storage::RelayKind::Push, "push-ctrl", R"({"target_ip":"127.0.0.1"})");
    expect_eq_int(relay_response.status_code, 200, "relay controller update push ok");
    expect_true(relay_controller_redis.hget(navcaster::redis_keys::PUSH_STAT, "push-ctrl").is_null(), "relay controller update clears push state");
    relay_response = relay_controller.list_records(navcaster::storage::RelayKind::Push);
    expect_eq_int(relay_response.status_code, 200, "relay controller list push ok");
    relay_controller_body = nlohmann::json::parse(relay_response.body);
    expect_true(relay_controller_body.contains("push-ctrl"), "relay controller list push contains record");

    if (failures != 0)
    {
        std::cerr << "[schema_smoke] failures: " << failures << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "[schema_smoke] all checks passed\n";
    return EXIT_SUCCESS;
}
