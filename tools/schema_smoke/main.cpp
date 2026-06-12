#include "account_repository.h"
#include "account_controller.h"
#include "account_schema.h"
#include "audit_log_service.h"
#include "auth_session_service.h"
#include "access_controller.h"
#include "access_policy_service.h"
#include "access_repository.h"
#include "alias_controller.h"
#include "alias_repository.h"
#include "broadcast_msg.h"
#include "cluster_monitor_repository.h"
#include "cluster_monitor_service.h"
#include "config_repository.h"
#include "config_controller.h"
#include "connection_history_repository.h"
#include "connection_history_service.h"
#include "json_record.h"
#include "mountpoint_subscriber_repository.h"
#include "mountpoint_subscriber_service.h"
#include "node_history_repository.h"
#include "node_history_service.h"
#include "node_log_level_service.h"
#include "redis_keys.h"
#include "redis_monitor_repository.h"
#include "redis_monitor_service.h"
#include "relay_controller.h"
#include "relay_repository.h"
#include "ring_log_service.h"
#include "runtime_command_service.h"
#include "runtime_state_controller.h"
#include "runtime_state_repository.h"
#include "sourcetable_service.h"
#include "statistics_controller.h"
#include "source_controller.h"
#include "source_repository.h"
#include "statistics_service.h"
#include "sse_snapshot_service.h"
#include "status_service.h"
#include "system_event_service.h"
#include "source_table_service.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
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

    bool setex(const char *key, int seconds, const std::string &value) override
    {
        strings[key] = parse_value(value);
        setex_calls.push_back({key, seconds, value});
        return set_ok;
    }

    bool publish(const char *channel, const std::string &message) override
    {
        publishes.push_back({channel, message});
        return publish_ok;
    }

    std::string info(const char *section = nullptr) override
    {
        if (section)
        {
            auto it = info_sections.find(section);
            if (it != info_sections.end())
            {
                return it->second;
            }
            return "";
        }
        return info_text;
    }

    long long dbsize() override
    {
        std::unordered_map<std::string, bool> keys;
        for (const auto &[key, value] : hashes)
        {
            (void)value;
            keys[key] = true;
        }
        for (const auto &[key, value] : lists)
        {
            (void)value;
            keys[key] = true;
        }
        for (const auto &[key, value] : strings)
        {
            (void)value;
            keys[key] = true;
        }
        return static_cast<long long>(keys.size());
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

    std::vector<std::string> scan_all_keys(int batch = 1000) override
    {
        (void)batch;
        std::vector<std::string> keys;
        for (const auto &[key, value] : hashes)
        {
            (void)value;
            keys.push_back(key);
        }
        for (const auto &[key, value] : lists)
        {
            (void)value;
            keys.push_back(key);
        }
        for (const auto &[key, value] : strings)
        {
            (void)value;
            keys.push_back(key);
        }
        return keys;
    }

    std::string type(const char *key) override
    {
        if (hashes.find(key) != hashes.end())
        {
            return "hash";
        }
        if (lists.find(key) != lists.end())
        {
            return "list";
        }
        if (strings.find(key) != strings.end())
        {
            return "string";
        }
        return "none";
    }

    long long hlen(const char *key) override
    {
        auto key_it = hashes.find(key);
        return key_it == hashes.end() ? 0 : static_cast<long long>(key_it->second.size());
    }

    long long llen(const char *key) override
    {
        auto key_it = lists.find(key);
        return key_it == lists.end() ? 0 : static_cast<long long>(key_it->second.size());
    }

    long long memory_usage(const char *key) override
    {
        auto it = memory.find(key);
        return it == memory.end() ? 0 : it->second;
    }

    long long incr(const char *key) override
    {
        return ++counters[key];
    }

    long long lpush(const char *key, const std::string &value) override
    {
        auto &list = lists[key];
        list.insert(list.begin(), parse_value(value));
        return static_cast<long long>(list.size());
    }

    bool ltrim(const char *key, long long start, long long stop) override
    {
        auto key_it = lists.find(key);
        if (key_it == lists.end())
        {
            return false;
        }
        auto &list = key_it->second;
        if (start < 0)
        {
            start = 0;
        }
        if (stop >= static_cast<long long>(list.size()))
        {
            stop = static_cast<long long>(list.size()) - 1;
        }
        if (list.empty() || stop < start)
        {
            list.clear();
            return true;
        }
        std::vector<nlohmann::json> trimmed;
        for (long long i = start; i <= stop; ++i)
        {
            trimmed.push_back(list[static_cast<std::size_t>(i)]);
        }
        list = std::move(trimmed);
        return true;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, nlohmann::json>> hashes;
    std::unordered_map<std::string, nlohmann::json> strings;
    std::unordered_map<std::string, std::vector<nlohmann::json>> lists;
    std::unordered_map<std::string, long long> counters;
    std::unordered_map<std::string, long long> memory;
    std::unordered_map<std::string, std::string> info_sections;
    std::vector<std::pair<std::string, std::string>> publishes;
    struct SetexCall
    {
        std::string key;
        int seconds = 0;
        std::string value;
    };
    std::vector<SetexCall> setex_calls;
    std::string info_text;
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

access_group make_access_policy_group(const std::string &uid, nlohmann::json overrides = nlohmann::json::object())
{
    nlohmann::json body = {{"uid", uid}, {"group_name", uid}};
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
    {
        body[it.key()] = it.value();
    }

    access_group group(uid);
    expect_eq_int(group.fromString(body.dump()), 0, "access policy group parse " + uid);
    return group;
}

access_item make_access_policy_item(const std::string &mount_point,
                                    caster::core::AccessState visible = caster::core::ACCESS_STATE_DEFALT,
                                    caster::core::AccessState access = caster::core::ACCESS_STATE_DEFALT,
                                    caster::core::AccessState nearby = caster::core::ACCESS_STATE_DEFALT)
{
    access_item item(mount_point);
    nlohmann::json body = {
        {"uid", mount_point},
        {"mountpoint", mount_point},
        {"allow_visible", static_cast<int>(visible)},
        {"allow_access", static_cast<int>(access)},
        {"allow_nearby", static_cast<int>(nearby)},
    };
    expect_eq_int(item.fromString(body.dump()), 0, "access policy item parse " + mount_point);
    return item;
}

source_record make_access_policy_source(const std::string &mount_point,
                                        const std::string &group_uid,
                                        nlohmann::json overrides = nlohmann::json::object())
{
    source_record source(mount_point);
    nlohmann::json body = {
        {"uid", mount_point},
        {"mountpoint", mount_point},
        {"source_group_uid", group_uid},
    };
    for (auto it = overrides.begin(); it != overrides.end(); ++it)
    {
        body[it.key()] = it.value();
    }
    expect_eq_int(source.fromString(body.dump()), 0, "access policy source parse " + mount_point);
    return source;
}

std::vector<std::string> split_fields(const std::string &line)
{
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ';'))
    {
        fields.push_back(field);
    }
    return fields;
}

std::unordered_map<std::string, std::vector<std::string>> source_table_by_mount(const std::string &text)
{
    std::unordered_map<std::string, std::vector<std::string>> result;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        std::size_t end = text.find("\r\n", pos);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        const std::string line = text.substr(pos, end - pos);
        if (!line.empty())
        {
            auto fields = split_fields(line);
            expect_true(fields.size() >= 19, "source table field count");
            if (fields.size() > 1)
            {
                result[fields[1]] = std::move(fields);
            }
        }
        pos = end + 2;
    }
    return result;
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

    int issued_token = 0;
    navcaster::http_api::AuthSessionService auth_sessions([&]() {
        return std::string("token-") + std::to_string(++issued_token);
    });
    auto auth_response = auth_sessions.login(
        R"({"username":"admin","password":"adminpass"})",
        {"admin", "adminpass"},
        nlohmann::json{{"admin_user", "redis-admin"}, {"admin_password", "redis-pass"}});
    expect_eq_int(auth_response.status_code, 200, "auth session default admin login status");
    auto auth_body = nlohmann::json::parse(auth_response.body);
    expect_eq(auth_body.value("token", std::string{}), "token-1", "auth session default admin token");
    expect_eq(auth_body.value("username", std::string{}), "admin", "auth session default admin username");
    expect_true(auth_sessions.validate_token("token-1"), "auth session validates default token");
    expect_eq(auth_sessions.lookup_user("token-1"), "admin", "auth session lookup default token");
    auth_response = auth_sessions.login(
        R"({"username":"redis-admin","password":"redis-pass"})",
        {"admin", "adminpass"},
        nlohmann::json{{"admin_user", "redis-admin"}, {"admin_password", "redis-pass"}});
    expect_eq_int(auth_response.status_code, 200, "auth session redis admin login status");
    auth_body = nlohmann::json::parse(auth_response.body);
    expect_eq(auth_body.value("token", std::string{}), "token-2", "auth session redis admin token");
    expect_eq(auth_sessions.lookup_user("token-2"), "redis-admin", "auth session lookup redis token");
    auth_response = auth_sessions.login("{", {"admin", "adminpass"}, nlohmann::json::object());
    expect_eq_int(auth_response.status_code, 400, "auth session invalid json");
    auth_response = auth_sessions.login(
        R"({"username":"admin","password":"wrong"})",
        {"admin", "adminpass"},
        nlohmann::json{{"admin_user", "redis-admin"}, {"admin_password", "redis-pass"}});
    expect_eq_int(auth_response.status_code, 401, "auth session invalid credentials");
    expect_true(!auth_sessions.validate_token(""), "auth session rejects empty token");
    expect_eq(auth_sessions.lookup_user("missing"), "", "auth session missing lookup");
    expect_eq(navcaster::http_api::bearer_token_from_authorization("Bearer token-1"), "token-1", "auth session bearer token");
    expect_eq(navcaster::http_api::bearer_token_from_authorization("Bad"), "", "auth session short authorization");
    auto logout_response = auth_sessions.logout("Bearer token-1");
    expect_eq_int(logout_response.status_code, 200, "auth session logout status");
    auth_body = nlohmann::json::parse(logout_response.body);
    expect_true(auth_body.value("ok", false), "auth session logout body");
    expect_true(!auth_sessions.validate_token("token-1"), "auth session logout invalidates");
    expect_true(auth_sessions.validate_token("token-2"), "auth session logout keeps other token");
    auth_sessions.invalidate_token("token-2");
    expect_true(!auth_sessions.validate_token("token-2"), "auth session explicit invalidate");

    FakeRedisHashClient subscriber_redis;
    subscriber_redis.hashes[redis_keys::MPT_LIST]["BASE01"] = "conn-a";
    subscriber_redis.hashes[redis_keys::MPT_LIST]["BASE02"] = "conn-b";
    subscriber_redis.hashes[redis_keys::mpt_sub("BASE01")]["client-a"] = {{"user", "u1"}};
    subscriber_redis.hashes[redis_keys::mpt_sub("BASE01")]["client-b"] = {{"user", "u2"}};
    subscriber_redis.hashes[redis_keys::mpt_sub("BASE03")]["ignored"] = {{"user", "u3"}};
    navcaster::storage::MountpointSubscriberRepository subscriber_repo(subscriber_redis);
    expect_true(subscriber_repo.online_mountpoints().contains("BASE01"), "mountpoint subscriber repository online list");
    expect_eq_int(static_cast<int>(subscriber_repo.subscriber_count("BASE01")), 2, "mountpoint subscriber repository count");
    expect_eq_int(static_cast<int>(subscriber_repo.subscriber_count("BASE02")), 0, "mountpoint subscriber repository empty count");
    navcaster::http_api::MountpointSubscriberService subscriber_service(subscriber_redis);
    auto subscriber_response = subscriber_service.list();
    expect_eq_int(subscriber_response.status_code, 200, "mountpoint subscriber service status");
    auto subscriber_body = nlohmann::json::parse(subscriber_response.body);
    expect_eq_int(subscriber_body.value("BASE01", -1), 2, "mountpoint subscriber service BASE01 count");
    expect_eq_int(subscriber_body.value("BASE02", -1), 0, "mountpoint subscriber service BASE02 count");
    expect_true(!subscriber_body.contains("BASE03"), "mountpoint subscriber service only online mountpoints");
    FakeRedisHashClient empty_subscriber_redis;
    navcaster::http_api::MountpointSubscriberService empty_subscriber_service(empty_subscriber_redis);
    subscriber_response = empty_subscriber_service.list();
    subscriber_body = nlohmann::json::parse(subscriber_response.body);
    expect_true(subscriber_body.is_object() && subscriber_body.empty(), "mountpoint subscriber service empty body");

    navcaster::http_api::StatusSnapshot status_snapshot;
    status_snapshot.cpu_percent = 12.5;
    status_snapshot.memory_bytes = 2 * 1024 * 1024;
    status_snapshot.caster_status = R"({"state":"running","connections":3})";
    status_snapshot.redis_caster_connected = true;
    status_snapshot.redis_auth_connected = false;
    status_snapshot.ntrip_port = 2101;
    status_snapshot.master_node = "node-a";
    status_snapshot.sse_clients = 4;
    status_snapshot.sse_max_clients = 200;
    status_snapshot.node_id = "node-a";
    status_snapshot.log_level = "info";
    auto status_body = navcaster::http_api::build_status_body(status_snapshot);
    expect_true(status_body["caster"].is_object(), "status service parses caster json");
    expect_eq(status_body["caster"].value("state", std::string{}), "running", "status service caster state");
    expect_true(status_body.value("cpu_percent", 0.0) == 12.5, "status service cpu");
    expect_eq_int(status_body.value("memory_bytes", 0), 2 * 1024 * 1024, "status service memory bytes");
    expect_true(status_body.value("memory_mb", 0.0) == 2.0, "status service memory mb");
    expect_true(status_body.value("redis_caster_connected", false), "status service caster redis");
    expect_true(!status_body.value("redis_auth_connected", true), "status service auth redis");
    expect_eq_int(status_body.value("ntrip_port", 0), 2101, "status service ntrip port");
    expect_eq(status_body.value("master_node", std::string{}), "node-a", "status service master node");
    expect_eq_int(status_body.value("sse_clients", 0), 4, "status service sse clients");
    expect_eq_int(status_body.value("sse_max_clients", 0), 200, "status service sse max");
    expect_eq(status_body.value("node_id", std::string{}), "node-a", "status service node id");
    expect_eq(status_body.value("log_level", std::string{}), "info", "status service log level");
    status_snapshot.caster_status = "plain-state";
    status_snapshot.master_node = {{"not", "string"}};
    status_body = navcaster::http_api::build_status_body(status_snapshot);
    expect_eq(status_body.value("caster", std::string{}), "plain-state", "status service caster string fallback");
    expect_true(status_body["master_node"].is_null(), "status service master node non-string null");
    navcaster::http_api::StatusService status_service;
    auto status_response = status_service.status(status_snapshot);
    expect_eq_int(status_response.status_code, 200, "status service status response code");
    status_body = nlohmann::json::parse(status_response.body);
    expect_eq(status_body.value("caster", std::string{}), "plain-state", "status service status response body");
    auto health_response = status_service.health();
    expect_eq_int(health_response.status_code, 200, "status service health status");
    auto health_body = nlohmann::json::parse(health_response.body);
    expect_eq(health_body.value("status", std::string{}), "ok", "status service health body");

    navcaster::http_api::NodeLogLevelService node_log_level_service;
    auto log_level_result = node_log_level_service.set_level("", "node-a", R"({"level":"info"})");
    expect_eq_int(log_level_result.response.status_code, 400, "node log level missing id");
    expect_true(!log_level_result.should_apply, "node log level missing id no apply");
    log_level_result = node_log_level_service.set_level("node-a", "node-a", "{");
    expect_eq_int(log_level_result.response.status_code, 400, "node log level invalid json");
    log_level_result = node_log_level_service.set_level("node-a", "node-a", R"({})");
    expect_eq_int(log_level_result.response.status_code, 400, "node log level missing level");
    log_level_result = node_log_level_service.set_level("node-b", "node-a", R"({"level":"info"})");
    expect_eq_int(log_level_result.response.status_code, 501, "node log level cross node");
    log_level_result = node_log_level_service.set_level("node-a", "node-a", R"({"level":"definitely-not-a-level"})");
    expect_eq_int(log_level_result.response.status_code, 400, "node log level unknown level");
    log_level_result = node_log_level_service.set_level("self", "node-a", R"({"level":"debug"})");
    expect_eq_int(log_level_result.response.status_code, 200, "node log level self ok");
    expect_true(log_level_result.should_apply, "node log level self apply");
    expect_true(log_level_result.level == spdlog::level::debug, "node log level parsed debug");
    auto log_level_body = nlohmann::json::parse(log_level_result.response.body);
    expect_eq(log_level_body.value("node_id", std::string{}), "node-a", "node log level self node id");
    expect_eq(log_level_body.value("level", std::string{}), "debug", "node log level self level");
    expect_true(log_level_body.value("ok", false), "node log level self ok body");
    log_level_result = node_log_level_service.set_level("current", "node-a", R"({"level":"off"})");
    expect_eq_int(log_level_result.response.status_code, 200, "node log level off ok");
    expect_true(log_level_result.level == spdlog::level::off, "node log level parsed off");
    log_level_result = node_log_level_service.set_level("node-a", "node-a", R"({"level":"warn"})");
    expect_eq_int(log_level_result.response.status_code, 200, "node log level current node ok");
    expect_true(log_level_result.level == spdlog::level::warn, "node log level parsed warn");

    FakeRedisHashClient monitor_capability_redis;
    monitor_capability_redis.hashes["HASH:ONE"]["field-a"] = {{"value", 1}};
    monitor_capability_redis.hashes["HASH:ONE"]["field-b"] = {{"value", 2}};
    monitor_capability_redis.lists["LIST:ONE"] = {
        {{"value", 1}},
        {{"value", 2}},
        {{"value", 3}}
    };
    monitor_capability_redis.strings["STRING:ONE"] = "value";
    monitor_capability_redis.memory["HASH:ONE"] = 123;
    monitor_capability_redis.info_text = "# Server\r\nredis_version:7.2.0\r\n";
    monitor_capability_redis.info_sections["stats"] = "# Stats\r\ninstantaneous_ops_per_sec:9\r\n";
    expect_eq_int(static_cast<int>(monitor_capability_redis.dbsize()), 3, "redis hash client fake dbsize");
    expect_eq(monitor_capability_redis.type("HASH:ONE"), "hash", "redis hash client fake hash type");
    expect_eq(monitor_capability_redis.type("LIST:ONE"), "list", "redis hash client fake list type");
    expect_eq(monitor_capability_redis.type("STRING:ONE"), "string", "redis hash client fake string type");
    expect_eq(monitor_capability_redis.type("MISSING"), "none", "redis hash client fake missing type");
    expect_eq_int(static_cast<int>(monitor_capability_redis.hlen("HASH:ONE")), 2, "redis hash client fake hlen");
    expect_eq_int(static_cast<int>(monitor_capability_redis.llen("LIST:ONE")), 3, "redis hash client fake llen");
    expect_eq_int(static_cast<int>(monitor_capability_redis.memory_usage("HASH:ONE")), 123, "redis hash client fake memory usage");
    expect_eq(monitor_capability_redis.info(), "# Server\r\nredis_version:7.2.0\r\n", "redis hash client fake info");
    expect_eq(monitor_capability_redis.info("stats"), "# Stats\r\ninstantaneous_ops_per_sec:9\r\n", "redis hash client fake info section");
    expect_eq(monitor_capability_redis.info("missing"), "", "redis hash client fake missing info section");

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

    FakeRedisHashClient system_event_redis;
    system_event_redis.hashes[redis_keys::log_node("node-a")]["100_start"] = {{"event", "start"}, {"node_id", "node-a"}, {"timestamp", 100}};
    system_event_redis.hashes[redis_keys::log_node("node-a")]["300_master"] = {{"event", "master_acquired"}, {"node_id", "node-a"}, {"timestamp", 300}};
    system_event_redis.hashes[redis_keys::log_node("node-a")]["bad"] = "ignored";
    system_event_redis.hashes[redis_keys::log_node("node-b")]["200_stop"] = {{"event", "stop"}, {"node_id", "node-b"}, {"timestamp", 200}};
    system_event_redis.hashes[redis_keys::log_mpt("MOUNT_A")]["not-node"] = {{"event", "ignored"}, {"timestamp", 999}};
    navcaster::storage::SystemEventRepository system_event_repo(system_event_redis);
    auto raw_node_events = system_event_repo.list_node_events();
    expect_eq_int(static_cast<int>(raw_node_events.size()), 3, "system event repository reads node hash events");
    navcaster::http_api::SystemEventService system_event_service(system_event_redis);
    auto system_event_response = system_event_service.list(2);
    expect_eq_int(system_event_response.status_code, 200, "system event service status");
    auto system_event_body = nlohmann::json::parse(system_event_response.body);
    expect_eq_int(system_event_body.value("count", 0), 2, "system event service applies limit");
    expect_eq(system_event_body["items"][0].value("event", std::string{}), "master_acquired", "system event service sorts newest first");
    expect_eq(system_event_body["items"][1].value("event", std::string{}), "stop", "system event service second newest");
    system_event_response = system_event_service.list(0);
    system_event_body = nlohmann::json::parse(system_event_response.body);
    expect_eq_int(system_event_body.value("count", 0), 3, "system event service bad limit defaults");

    FakeRedisHashClient cluster_redis;
    cluster_redis.strings[redis_keys::CASTER_MASTER] = "node-a";
    cluster_redis.hashes[redis_keys::CASTER_NODE]["node-a"] = {
        {"node_name", "alpha"},
        {"server_count", 2},
        {"client_count", 3},
        {"connect_count", 5},
        {"cpu_usage", 1.5},
        {"mem_usage", 50.0},
        {"send_speed", 4.0},
        {"recv_speed", 5.0},
        {"send_total", 40},
        {"recv_total", 50},
        {"set_version", "s1"},
        {"tag_version", "t1"},
        {"queue_delay", 2},
        {"hostname", "host-a"},
        {"listen_port", 2101},
        {"http_port", 8080},
        {"process_id", 12345},
        {"http_enabled", true},
        {"online_time", 900},
        {"update_time", 990},
        {"pub_ping_delay", 7},
        {"sub_ping_delay", 8}
    };
    cluster_redis.hashes[redis_keys::CASTER_NODE]["node-b"] = {
        {"node_name", "beta"},
        {"server_count", 20},
        {"client_count", 30},
        {"connect_count", 50},
        {"cpu_usage", 9.0},
        {"mem_usage", 90.0},
        {"send_speed", 40.0},
        {"recv_speed", 50.0},
        {"online_time", 800},
        {"update_time", 900}
    };
    cluster_redis.hashes[redis_keys::CASTER_NODE]["node-zero"] = {
        {"node_name", "zero"},
        {"server_count", 4},
        {"client_count", 6},
        {"cpu_usage", 2.5},
        {"mem_usage", 20.0},
        {"send_speed", 7.0},
        {"recv_speed", 8.0},
        {"online_time", 950},
        {"update_time", 0}
    };
    cluster_redis.hashes[redis_keys::CASTER_NODE]["bad-node"] = "not-an-object";
    cluster_redis.hashes[redis_keys::PULL_STAT]["pull-a"] = {{"state", 1}, {"node_uid", "node-a"}};
    cluster_redis.hashes[redis_keys::PULL_STAT]["pull-b"] = nlohmann::json{{"state", 1}, {"node_uid", "node-b"}}.dump();
    cluster_redis.hashes[redis_keys::PULL_STAT]["pull-no-node"] = {{"state", 1}};
    cluster_redis.hashes[redis_keys::PULL_STAT]["pull-stopped"] = {{"state", 0}, {"node_uid", "node-a"}};
    cluster_redis.hashes[redis_keys::PULL_STAT]["pull-bad"] = "{";
    cluster_redis.hashes[redis_keys::PUSH_STAT]["push-zero"] = {{"state", 1}, {"node_uid", "node-zero"}};
    cluster_redis.hashes[redis_keys::PUSH_STAT]["push-stopped"] = {{"state", 0}, {"node_uid", "node-a"}};
    navcaster::storage::ClusterMonitorRepository cluster_repo(cluster_redis);
    expect_eq(cluster_repo.master_node(), "node-a", "cluster monitor repository master");
    expect_true(cluster_repo.nodes().contains("node-a"), "cluster monitor repository nodes");
    auto cluster_snapshot = navcaster::http_api::build_cluster_monitor_snapshot(
        cluster_repo.master_node(),
        cluster_repo.nodes(),
        cluster_repo.pull_states(),
        cluster_repo.push_states(),
        1000,
        12.5);
    expect_eq(cluster_snapshot.value("master_node", std::string{}), "node-a", "cluster monitor snapshot master");
    expect_eq_int(cluster_snapshot.value("total_nodes", 0), 4, "cluster monitor counts non-object node");
    expect_eq_int(cluster_snapshot.value("online_nodes", 0), 2, "cluster monitor online nodes");
    expect_eq_int(cluster_snapshot.value("total_servers", 0), 6, "cluster monitor totals online servers");
    expect_eq_int(cluster_snapshot.value("total_clients", 0), 9, "cluster monitor totals online clients");
    expect_eq_int(cluster_snapshot.value("total_pull", 0), 3, "cluster monitor counts running pulls");
    expect_eq_int(cluster_snapshot.value("total_push", 0), 1, "cluster monitor counts running pushes");
    expect_true(cluster_snapshot.value("total_cpu", 0.0) == 4.0, "cluster monitor totals cpu");
    expect_true(cluster_snapshot.value("total_mem", 0.0) == 70.0, "cluster monitor totals memory");
    expect_true(cluster_snapshot.value("total_send_speed", 0.0) == 11.0, "cluster monitor totals send speed");
    expect_true(cluster_snapshot.value("total_recv_speed", 0.0) == 13.0, "cluster monitor totals recv speed");
    expect_true(cluster_snapshot.value("redis_latency_ms", 0.0) == 12.5, "cluster monitor fixed latency");
    expect_eq_int(static_cast<int>(cluster_snapshot["nodes"].size()), 3, "cluster monitor skips non-object node item");
    auto find_cluster_node = [](const nlohmann::json &nodes, const std::string &uid) {
        for (const auto &node : nodes)
        {
            if (node.value("uid", std::string{}) == uid)
            {
                return node;
            }
        }
        return nlohmann::json::object();
    };
    auto node_a_snapshot = find_cluster_node(cluster_snapshot["nodes"], "node-a");
    expect_true(node_a_snapshot.value("is_master", false), "cluster monitor marks master");
    expect_true(node_a_snapshot.value("online", false), "cluster monitor marks recent node online");
    expect_eq_int(node_a_snapshot.value("pull", 0), 1, "cluster monitor node pull count");
    expect_eq_int(node_a_snapshot.value("push", 0), 0, "cluster monitor node push count");
    expect_eq_int(node_a_snapshot.value("uptime_sec", 0), 100, "cluster monitor uptime");
    expect_eq(node_a_snapshot.value("hostname", std::string{}), "host-a", "cluster monitor preserves hostname");
    auto node_b_snapshot = find_cluster_node(cluster_snapshot["nodes"], "node-b");
    expect_true(!node_b_snapshot.value("online", true), "cluster monitor marks stale node offline");
    expect_eq_int(node_b_snapshot.value("pull", 0), 1, "cluster monitor counts string relay JSON");
    auto node_zero_snapshot = find_cluster_node(cluster_snapshot["nodes"], "node-zero");
    expect_true(node_zero_snapshot.value("online", false), "cluster monitor treats zero update as online");
    expect_eq_int(node_zero_snapshot.value("push", 0), 1, "cluster monitor node push count");
    navcaster::http_api::ClusterMonitorService cluster_service(cluster_redis);
    auto cluster_response = cluster_service.snapshot(1000);
    expect_eq_int(cluster_response.status_code, 200, "cluster monitor service status");
    auto cluster_body = nlohmann::json::parse(cluster_response.body);
    expect_eq(cluster_body.value("master_node", std::string{}), "node-a", "cluster monitor service body");

    expect_eq_int(static_cast<int>(navcaster::http_api::redis_monitor_history_limit("")), 60, "redis monitor history default range");
    expect_eq_int(static_cast<int>(navcaster::http_api::redis_monitor_history_limit("1h")), 60, "redis monitor history 1h range");
    expect_eq_int(static_cast<int>(navcaster::http_api::redis_monitor_history_limit("6h")), 360, "redis monitor history 6h range");
    expect_eq_int(static_cast<int>(navcaster::http_api::redis_monitor_history_limit("24h")), 1440, "redis monitor history 24h range");
    expect_eq_int(static_cast<int>(navcaster::http_api::redis_monitor_history_limit("7d")), 10080, "redis monitor history 7d range");
    expect_eq_int(static_cast<int>(navcaster::http_api::redis_monitor_history_limit("bad")), 60, "redis monitor history bad range defaults");
    FakeRedisHashClient redis_monitor_redis;
    for (int t = 65; t >= 1; --t)
    {
        redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].push_back({{"t", t}, {"used_memory", t * 10}});
    }
    redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].insert(
        redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].begin(),
        nlohmann::json{{"t", 66}, {"used_memory", 0}});
    redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].insert(
        redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].begin(),
        nlohmann::json{{"t", 0}, {"used_memory", 670}});
    redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].insert(
        redis_monitor_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].begin(),
        "bad");
    navcaster::storage::RedisMonitorRepository redis_monitor_repo(redis_monitor_redis);
    auto redis_monitor_raw = redis_monitor_repo.history(2);
    expect_eq_int(static_cast<int>(redis_monitor_raw.size()), 2, "redis monitor repository applies limit");
    expect_true(redis_monitor_raw[0].is_string(), "redis monitor repository keeps newest item");
    auto redis_monitor_items = navcaster::http_api::redis_monitor_history_items(redis_monitor_raw);
    expect_eq_int(static_cast<int>(redis_monitor_items.size()), 0, "redis monitor history helper filters invalid recent items");
    navcaster::http_api::RedisMonitorService redis_monitor_service(redis_monitor_redis);
    auto redis_monitor_response = redis_monitor_service.history("");
    expect_eq_int(redis_monitor_response.status_code, 200, "redis monitor history service status");
    auto redis_monitor_body = nlohmann::json::parse(redis_monitor_response.body);
    expect_eq_int(redis_monitor_body.value("count", 0), 57, "redis monitor history service defaults to 60 raw points");
    expect_eq_int(redis_monitor_body["items"][0].value("t", 0), 9, "redis monitor history service returns ascending time");
    expect_eq_int(redis_monitor_body["items"].back().value("t", 0), 65, "redis monitor history service newest retained");
    redis_monitor_response = redis_monitor_service.history("7d");
    redis_monitor_body = nlohmann::json::parse(redis_monitor_response.body);
    expect_eq_int(redis_monitor_body.value("count", 0), 65, "redis monitor history service 7d reads all valid points");
    expect_eq_int(redis_monitor_body["items"][0].value("t", 0), 1, "redis monitor history service 7d oldest first");
    FakeRedisHashClient empty_redis_monitor_redis;
    navcaster::http_api::RedisMonitorService empty_redis_monitor_service(empty_redis_monitor_redis);
    redis_monitor_response = empty_redis_monitor_service.history("24h");
    redis_monitor_body = nlohmann::json::parse(redis_monitor_response.body);
    expect_eq_int(redis_monitor_body.value("count", -1), 0, "redis monitor history service empty list count");
    expect_true(redis_monitor_body["items"].is_array() && redis_monitor_body["items"].empty(), "redis monitor history service empty list items");
    const std::string redis_info_text =
        "# Server\r\n"
        "redis_version:7.2.1\r\n"
        "uptime_in_seconds:123\r\n"
        "tcp_port:6379\r\n"
        "os:Windows 10\r\n"
        "process_id:456\r\n"
        "# Clients\r\n"
        "connected_clients:7\r\n"
        "blocked_clients:1\r\n"
        "maxclients:10000\r\n"
        "# Memory\r\n"
        "used_memory:2048\r\n"
        "used_memory_human:2.00K\r\n"
        "used_memory_rss:4096\r\n"
        "used_memory_rss_human:4.00K\r\n"
        "used_memory_peak:8192\r\n"
        "used_memory_peak_human:8.00K\r\n"
        "mem_fragmentation_ratio:1.25\r\n"
        "# Stats\r\n"
        "total_connections_received:10\r\n"
        "total_commands_processed:20\r\n"
        "instantaneous_ops_per_sec:3\r\n"
        "keyspace_hits:3\r\n"
        "keyspace_misses:1\r\n"
        "instantaneous_input_kbps:1.5\r\n"
        "instantaneous_output_kbps:2.5\r\n"
        "mixed_value:12abc\r\n"
        "# Replication\r\n"
        "role:master\r\n"
        "connected_slaves:0\r\n"
        "# Keyspace\r\n"
        "db0:keys=5,expires=0,avg_ttl=0\r\n";
    auto parsed_redis_info = navcaster::http_api::parse_redis_info(redis_info_text);
    expect_eq(parsed_redis_info["server"].value("redis_version", std::string{}), "7.2.1", "redis monitor parses redis version");
    expect_eq_int(parsed_redis_info["server"].value("uptime_in_seconds", 0), 123, "redis monitor parses integer");
    expect_true(parsed_redis_info["memory"].value("mem_fragmentation_ratio", 0.0) == 1.25, "redis monitor parses float");
    expect_eq(parsed_redis_info["stats"].value("mixed_value", std::string{}), "12abc", "redis monitor keeps mixed string");
    expect_eq(parsed_redis_info["keyspace"].value("db0", std::string{}), "keys=5,expires=0,avg_ttl=0", "redis monitor keeps keyspace string");
    auto redis_summary_body = navcaster::http_api::redis_monitor_summary_body(parsed_redis_info, 42);
    expect_eq(redis_summary_body["server"].value("os", std::string{}), "Windows 10", "redis monitor summary server os");
    expect_eq_int(redis_summary_body["clients"].value("connected_clients", 0), 7, "redis monitor summary clients");
    expect_eq_int(redis_summary_body["memory"].value("used_memory", 0), 2048, "redis monitor summary memory");
    expect_true(redis_summary_body["stats"].value("hit_rate", 0.0) == 0.75, "redis monitor summary hit rate");
    expect_eq(redis_summary_body["replication"].value("role", std::string{}), "master", "redis monitor summary replication");
    expect_eq_int(redis_summary_body.value("total_keys", 0), 42, "redis monitor summary total keys");
    auto redis_summary_missing_stats = navcaster::http_api::redis_monitor_summary_body(nlohmann::json{{"server", nlohmann::json::object()}}, 0);
    expect_true(!redis_summary_missing_stats.contains("stats"), "redis monitor summary tolerates missing stats");
    FakeRedisHashClient redis_summary_redis;
    redis_summary_redis.info_text = redis_info_text;
    redis_summary_redis.hashes["HASH:SUMMARY"]["field"] = {{"value", 1}};
    redis_summary_redis.lists["LIST:SUMMARY"] = {nlohmann::json{{"value", 1}}};
    redis_summary_redis.strings["STRING:SUMMARY"] = "value";
    navcaster::http_api::RedisMonitorService redis_summary_service(redis_summary_redis);
    auto redis_summary_response = redis_summary_service.summary();
    expect_eq_int(redis_summary_response.status_code, 200, "redis monitor summary service status");
    auto redis_summary_service_body = nlohmann::json::parse(redis_summary_response.body);
    expect_eq_int(redis_summary_service_body.value("total_keys", 0), 3, "redis monitor summary service dbsize");
    FakeRedisHashClient redis_summary_empty_info;
    navcaster::http_api::RedisMonitorService redis_summary_empty_service(redis_summary_empty_info);
    redis_summary_response = redis_summary_empty_service.summary();
    expect_eq_int(redis_summary_response.status_code, 503, "redis monitor summary empty info status");
    redis_summary_service_body = nlohmann::json::parse(redis_summary_response.body);
    expect_eq(redis_summary_service_body.value("error", std::string{}), "Redis not available", "redis monitor summary empty info error");
    expect_eq(navcaster::http_api::redis_monitor_key_prefix(redis_keys::mpt_rec("BASE01")), "MPT:REC", "redis monitor key prefix known mpt rec");
    expect_eq(navcaster::http_api::redis_monitor_key_prefix(redis_keys::access_item("default")), "ACCESS:ITEM", "redis monitor key prefix known access item");
    expect_eq(navcaster::http_api::redis_monitor_key_prefix("A:B:C"), "A:B", "redis monitor key prefix unknown two segments");
    expect_eq(navcaster::http_api::redis_monitor_key_prefix("PLAINKEY"), "PLAINKEY", "redis monitor key prefix plain");
    FakeRedisHashClient redis_keys_redis;
    redis_keys_redis.hashes[redis_keys::mpt_rec("BASE01")]["conn-a"] = {{"t", 1}};
    redis_keys_redis.hashes[redis_keys::mpt_rec("BASE01")]["conn-b"] = {{"t", 2}};
    redis_keys_redis.hashes[redis_keys::access_item("default")]["BASE01"] = {{"mount_point_name", "BASE01"}};
    redis_keys_redis.lists[redis_keys::LOG_AUDIT] = {
        {{"id", 2}},
        {{"id", 1}}
    };
    redis_keys_redis.strings["A:B:C"] = "unknown";
    redis_keys_redis.strings["PLAINKEY"] = "plain";
    redis_keys_redis.memory[redis_keys::mpt_rec("BASE01")] = 100;
    redis_keys_redis.memory[redis_keys::access_item("default")] = 30;
    redis_keys_redis.memory[redis_keys::LOG_AUDIT] = 70;
    redis_keys_redis.memory["A:B:C"] = 5;
    redis_keys_redis.memory["PLAINKEY"] = 2;
    navcaster::http_api::RedisMonitorService redis_keys_service(redis_keys_redis);
    auto redis_keys_response = redis_keys_service.keys();
    expect_eq_int(redis_keys_response.status_code, 200, "redis monitor keys service status");
    auto redis_keys_body = nlohmann::json::parse(redis_keys_response.body);
    expect_eq_int(redis_keys_body.value("total_keys", 0), 5, "redis monitor keys total keys");
    expect_eq_int(redis_keys_body.value("total_memory", 0), 207, "redis monitor keys total memory");
    auto find_redis_category = [](const nlohmann::json &categories, const std::string &prefix) {
        for (const auto &category : categories)
        {
            if (category.value("prefix", std::string{}) == prefix)
            {
                return category;
            }
        }
        return nlohmann::json::object();
    };
    auto mpt_rec_category = find_redis_category(redis_keys_body["categories"], "MPT:REC");
    expect_eq(mpt_rec_category.value("type", std::string{}), "hash", "redis monitor keys mpt rec type");
    expect_eq_int(mpt_rec_category.value("count", 0), 1, "redis monitor keys mpt rec count");
    expect_eq_int(mpt_rec_category.value("fields", 0), 2, "redis monitor keys mpt rec fields");
    expect_eq_int(mpt_rec_category.value("memory", 0), 100, "redis monitor keys mpt rec memory");
    expect_eq(mpt_rec_category.value("description", std::string{}), "挂载点连接列表（connect_key→登录时间）", "redis monitor keys mpt rec description");
    auto access_item_category = find_redis_category(redis_keys_body["categories"], "ACCESS:ITEM");
    expect_eq_int(access_item_category.value("fields", 0), 1, "redis monitor keys access item fields");
    expect_eq(access_item_category.value("description", std::string{}), "访问控制组成员项", "redis monitor keys access item description");
    auto audit_category = find_redis_category(redis_keys_body["categories"], "LOG:AUDIT");
    expect_eq(audit_category.value("type", std::string{}), "list", "redis monitor keys audit type");
    expect_eq_int(audit_category.value("fields", 0), 2, "redis monitor keys audit fields");
    auto unknown_category = find_redis_category(redis_keys_body["categories"], "A:B");
    expect_eq(unknown_category.value("type", std::string{}), "string", "redis monitor keys unknown type");
    expect_eq_int(unknown_category.value("fields", -1), 0, "redis monitor keys unknown fields");
    expect_eq(unknown_category.value("description", std::string{}), "", "redis monitor keys unknown description");
    auto plain_category = find_redis_category(redis_keys_body["categories"], "PLAINKEY");
    expect_eq(plain_category.value("type", std::string{}), "string", "redis monitor keys plain type");
    FakeRedisHashClient redis_sampler_redis;
    redis_sampler_redis.info_sections["ALL"] =
        "# Memory\r\n"
        "used_memory:2048\r\n"
        "used_memory_rss:4096\r\n"
        "used_memory_peak:8192\r\n"
        "mem_fragmentation_ratio:1.5\r\n"
        "# Stats\r\n"
        "instantaneous_ops_per_sec:11\r\n"
        "total_commands_processed:22\r\n"
        "total_connections_received:33\r\n"
        "keyspace_hits:3\r\n"
        "keyspace_misses:1\r\n"
        "instantaneous_input_kbps:4.5\r\n"
        "instantaneous_output_kbps:5.5\r\n"
        "# Clients\r\n"
        "connected_clients:6\r\n"
        "blocked_clients:2\r\n";
    redis_sampler_redis.hashes["HASH:SAMPLE"]["field"] = {{"value", 1}};
    navcaster::http_api::RedisMonitorService redis_sampler_service(redis_sampler_redis);
    expect_true(redis_sampler_service.sample_history(123456), "redis monitor sampler writes valid sample");
    expect_eq_int(static_cast<int>(redis_sampler_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].size()), 1, "redis monitor sampler list size");
    auto redis_sample_point = redis_sampler_redis.lists[redis_keys::MONITOR_REDIS_HISTORY][0];
    expect_eq_int(redis_sample_point.value("t", 0), 123456, "redis monitor sampler timestamp");
    expect_eq_int(redis_sample_point.value("used_memory", 0), 2048, "redis monitor sampler used memory");
    expect_eq_int(redis_sample_point.value("used_memory_rss", 0), 4096, "redis monitor sampler rss");
    expect_eq_int(redis_sample_point.value("used_memory_peak", 0), 8192, "redis monitor sampler peak");
    expect_true(redis_sample_point.value("mem_fragmentation_ratio", 0.0) == 1.5, "redis monitor sampler fragmentation");
    expect_eq_int(redis_sample_point.value("total_keys", 0), 1, "redis monitor sampler dbsize before history write");
    expect_eq_int(redis_sample_point.value("ops_per_sec", 0), 11, "redis monitor sampler ops");
    expect_eq_int(redis_sample_point.value("total_commands_processed", 0), 22, "redis monitor sampler commands");
    expect_eq_int(redis_sample_point.value("total_connections_received", 0), 33, "redis monitor sampler connections");
    expect_eq_int(redis_sample_point.value("hits", 0), 3, "redis monitor sampler hits");
    expect_eq_int(redis_sample_point.value("misses", 0), 1, "redis monitor sampler misses");
    expect_true(redis_sample_point.value("hit_rate", 0.0) == 0.75, "redis monitor sampler hit rate");
    expect_eq_int(redis_sample_point.value("connected_clients", 0), 6, "redis monitor sampler connected clients");
    expect_eq_int(redis_sample_point.value("blocked_clients", 0), 2, "redis monitor sampler blocked clients");
    expect_true(redis_sample_point.value("input_kbps", 0.0) == 4.5, "redis monitor sampler input kbps");
    expect_true(redis_sample_point.value("output_kbps", 0.0) == 5.5, "redis monitor sampler output kbps");
    for (int i = 0; i < 10090; ++i)
    {
        redis_sampler_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].push_back({{"t", i + 1}, {"used_memory", 1}});
    }
    expect_true(redis_sampler_service.sample_history(123457), "redis monitor sampler writes before trim");
    expect_eq_int(static_cast<int>(redis_sampler_redis.lists[redis_keys::MONITOR_REDIS_HISTORY].size()), static_cast<int>(navcaster::http_api::REDIS_MONITOR_HISTORY_KEEP), "redis monitor sampler trims history");
    FakeRedisHashClient redis_sampler_empty_info;
    navcaster::http_api::RedisMonitorService redis_sampler_empty_service(redis_sampler_empty_info);
    expect_true(!redis_sampler_empty_service.sample_history(1), "redis monitor sampler skips empty info");
    expect_true(redis_sampler_empty_info.lists[redis_keys::MONITOR_REDIS_HISTORY].empty(), "redis monitor sampler empty info writes nothing");
    FakeRedisHashClient redis_sampler_missing_section;
    redis_sampler_missing_section.info_sections["ALL"] = "# Memory\r\nused_memory:1\r\n# Stats\r\nkeyspace_hits:1\r\n";
    navcaster::http_api::RedisMonitorService redis_sampler_missing_service(redis_sampler_missing_section);
    expect_true(!redis_sampler_missing_service.sample_history(1), "redis monitor sampler skips missing clients");
    expect_true(redis_sampler_missing_section.lists[redis_keys::MONITOR_REDIS_HISTORY].empty(), "redis monitor sampler missing section writes nothing");
    FakeRedisHashClient redis_sampler_zero_memory;
    redis_sampler_zero_memory.info_sections["ALL"] = "# Memory\r\nused_memory:0\r\n# Stats\r\nkeyspace_hits:1\r\n# Clients\r\nconnected_clients:1\r\n";
    navcaster::http_api::RedisMonitorService redis_sampler_zero_service(redis_sampler_zero_memory);
    expect_true(!redis_sampler_zero_service.sample_history(1), "redis monitor sampler skips zero memory");
    expect_true(redis_sampler_zero_memory.lists[redis_keys::MONITOR_REDIS_HISTORY].empty(), "redis monitor sampler zero memory writes nothing");

    expect_eq_int(navcaster::http_api::infer_ring_log_level("[trace] detail"), 0, "ring log infers trace");
    expect_eq_int(navcaster::http_api::infer_ring_log_level("[debug] detail"), 1, "ring log infers debug");
    expect_eq_int(navcaster::http_api::infer_ring_log_level("[info] detail"), 2, "ring log infers info");
    expect_eq_int(navcaster::http_api::infer_ring_log_level("[warn] detail"), 3, "ring log infers warn");
    expect_eq_int(navcaster::http_api::infer_ring_log_level("[error] detail"), 4, "ring log infers error");
    expect_eq_int(navcaster::http_api::infer_ring_log_level("[critical] detail"), 5, "ring log infers critical");
    expect_eq_int(static_cast<int>(navcaster::http_api::normalize_ring_log_count(0)), 500, "ring log zero count defaults");
    expect_eq_int(static_cast<int>(navcaster::http_api::normalize_ring_log_count(5001)), 500, "ring log large count defaults");
    std::size_t requested_ring_count = 0;
    navcaster::http_api::RingLogService ring_log_service(
        [&](std::size_t count) {
            requested_ring_count = count;
            return std::vector<std::string>{
                "[debug] hidden",
                "[info] visible",
                "[error] visible"
            };
        });
    auto ring_response = ring_log_service.list(0, "info", 123456);
    expect_eq_int(static_cast<int>(requested_ring_count), 500, "ring log service normalizes count before read");
    expect_eq_int(ring_response.status_code, 200, "ring log service status");
    auto ring_body = nlohmann::json::parse(ring_response.body);
    expect_eq_int(ring_body.value("count", 0), 2, "ring log service filters level");
    expect_eq_int(ring_body["items"][0].value("timestamp", 0), 123456, "ring log service timestamp");
    expect_eq(ring_body["items"][0].value("message", std::string{}), "[info] visible", "ring log service first visible");
    expect_eq_int(ring_body["items"][1].value("level", 0), 4, "ring log service error level");

    nlohmann::json audit_payload = {{"password", "secret"}, {"nested", {{"token", "abc"}, {"safe", "ok"}}}};
    navcaster::http_api::mask_audit_secrets(audit_payload);
    expect_eq(audit_payload.value("password", std::string{}), "***", "audit masks password");
    expect_eq(audit_payload["nested"].value("token", std::string{}), "***", "audit masks nested token");
    expect_eq(audit_payload["nested"].value("safe", std::string{}), "ok", "audit keeps safe field");
    auto audit_target = navcaster::http_api::infer_audit_target("/api/accounts/demo");
    expect_eq(audit_target.type, "accounts", "audit target type");
    expect_eq(audit_target.id, "demo", "audit target id");
    expect_true(!navcaster::http_api::should_write_audit(EVHTTP_REQ_GET, "/api/accounts"), "audit skips get");
    expect_true(!navcaster::http_api::should_write_audit(EVHTTP_REQ_POST, "/api/auth/login"), "audit skips login");
    expect_true(navcaster::http_api::should_write_audit(EVHTTP_REQ_PUT, "/api/accounts/demo"), "audit records put");

    FakeRedisHashClient audit_redis;
    navcaster::http_api::AuditLogService audit_service(audit_redis);
    HttpRequest audit_req;
    audit_req.method = EVHTTP_REQ_PUT;
    audit_req.path = "/api/accounts/demo";
    audit_req.body = R"({"password":"secret","nested":{"admin_password":"root","safe":"ok"}})";
    HttpResponse audit_resp;
    audit_resp.status_code = 400;
    audit_resp.body = R"({"error":"bad input"})";
    audit_service.write(audit_req, audit_resp, "alice", "127.0.0.1", "node-1", 12345);
    expect_eq_int(static_cast<int>(audit_redis.lists[redis_keys::LOG_AUDIT].size()), 1, "audit service writes one record");
    auto audit_record = audit_redis.lists[redis_keys::LOG_AUDIT][0];
    expect_eq_int(audit_record.value("id", 0), 1, "audit service id");
    expect_eq(audit_record.value("actor", std::string{}), "alice", "audit service actor");
    expect_eq(audit_record.value("target_type", std::string{}), "accounts", "audit service target type");
    expect_eq(audit_record.value("target_id", std::string{}), "demo", "audit service target id");
    expect_eq(audit_record.value("error_message", std::string{}), "bad input", "audit service error message");
    auto stored_payload = nlohmann::json::parse(audit_record.value("payload", std::string{}));
    expect_eq(stored_payload.value("password", std::string{}), "***", "audit service stored masked password");
    expect_eq(stored_payload["nested"].value("admin_password", std::string{}), "***", "audit service stored masked nested admin password");
    audit_req.method = EVHTTP_REQ_POST;
    audit_req.path = "/api/auth/login";
    audit_service.write(audit_req, audit_resp, "alice", "127.0.0.1", "node-1", 12346);
    expect_eq_int(static_cast<int>(audit_redis.lists[redis_keys::LOG_AUDIT].size()), 1, "audit service skips login write");
    audit_redis.lists[redis_keys::LOG_AUDIT].push_back({{"actor", "bob"}, {"action", "DELETE /api/sources/SRC"}, {"target_type", "sources"}});
    auto audit_list_response = audit_service.list(1, 0, "alice", "PUT", "accounts");
    expect_eq_int(audit_list_response.status_code, 200, "audit service list status");
    auto audit_list_body = nlohmann::json::parse(audit_list_response.body);
    expect_eq_int(audit_list_body.value("total", 0), 2, "audit service list total");
    expect_eq_int(audit_list_body.value("next_cursor", 0), 1, "audit service list next cursor");
    expect_true(audit_list_body.value("has_more", false), "audit service list has more");
    expect_eq(audit_list_body["items"][0].value("actor", std::string{}), "alice", "audit service list filter result");

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

    FakeRedisHashClient statistics_controller_redis;
    statistics_controller_redis.hashes[navcaster::redis_keys::log_mpt("MOUNT_A")]["srv1"] =
        {{"type", 1}, {"name", "MOUNT_A"}, {"connect_time", 0}, {"disconnect_time", 3600}};
    statistics_controller_redis.hashes[navcaster::redis_keys::log_usr("user1")]["usr1"] =
        {{"type", 2}, {"name", "user1"}, {"mount", "MOUNT_A"}, {"connect_time", 0}, {"disconnect_time", 1800}};
    statistics_controller_redis.hashes[navcaster::redis_keys::log_mpt("MOUNT_B")]["srv2"] =
        {{"type", 1}, {"name", "MOUNT_B"}, {"connect_time", 100000}, {"disconnect_time", 101000}};
    navcaster::http_api::StatisticsController statistics_controller(statistics_controller_redis, 200000);
    expect_eq(navcaster::redis_keys::stat_daily("1970-01-01"), "STAT:DAILY:1970-01-01", "statistics daily cache key");
    expect_eq_int(navcaster::http_api::statistics_limit_param({{"limit", "0"}}), 20, "statistics controller zero limit defaults");
    expect_eq_int(navcaster::http_api::statistics_limit_param({{"limit", "999"}}), 100, "statistics controller caps limit");

    auto statistics_controller_response = statistics_controller.overview({{"date", "1970-01-01"}, {"start", "100000"}});
    expect_eq_int(statistics_controller_response.status_code, 200, "statistics controller overview status");
    auto statistics_controller_body = nlohmann::json::parse(statistics_controller_response.body);
    expect_true(statistics_controller_body.value("start", -1) != 100000, "statistics controller date overrides start query");
    expect_eq_int(statistics_controller_body.value("mpt_connections", 0), 1, "statistics controller overview reads mpt logs");
    expect_eq_int(statistics_controller_body.value("usr_connections", 0), 1, "statistics controller overview reads usr logs");

    statistics_controller_response = statistics_controller.daily("bad-date");
    expect_eq_int(statistics_controller_response.status_code, 400, "statistics controller invalid daily date");
    statistics_controller_redis.strings[navcaster::redis_keys::stat_daily("1970-01-01")] = R"({"cached":true})";
    statistics_controller_response = statistics_controller.daily("1970-01-01");
    expect_eq_int(statistics_controller_response.status_code, 200, "statistics controller cached daily status");
    statistics_controller_body = nlohmann::json::parse(statistics_controller_response.body);
    expect_true(statistics_controller_body.value("cached", false), "statistics controller returns cached daily body");

    statistics_controller_redis.strings.erase(navcaster::redis_keys::stat_daily("1970-01-01"));
    statistics_controller_response = statistics_controller.daily("1970-01-01");
    expect_eq_int(statistics_controller_response.status_code, 200, "statistics controller daily computes status");
    expect_eq_int(static_cast<int>(statistics_controller_redis.setex_calls.size()), 1, "statistics controller caches past day");
    expect_eq(statistics_controller_redis.setex_calls[0].key, navcaster::redis_keys::stat_daily("1970-01-01"), "statistics controller cache key");
    expect_eq_int(statistics_controller_redis.setex_calls[0].seconds, 604800, "statistics controller cache ttl");

    statistics_controller_response = statistics_controller.mountpoint_ranking({{"start", "1"}, {"end", "200000"}, {"limit", "1"}});
    statistics_controller_body = nlohmann::json::parse(statistics_controller_response.body);
    expect_eq_int(static_cast<int>(statistics_controller_body.size()), 1, "statistics controller mount ranking limit");
    statistics_controller_response = statistics_controller.user_ranking({{"start", "1"}, {"end", "200000"}, {"limit", "999"}});
    statistics_controller_body = nlohmann::json::parse(statistics_controller_response.body);
    expect_eq_int(static_cast<int>(statistics_controller_body.size()), 1, "statistics controller user ranking reads logs");

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

    navcaster::core::SourceRecordMap access_sources;
    navcaster::core::SourceRecordMap access_decodes;
    navcaster::core::AccessGroupMap access_groups;
    navcaster::core::AccessItemMap access_items;
    navcaster::core::AccessPolicyService empty_access_policy(access_sources, access_decodes, access_groups, access_items);
    std::string access_reason;
    expect_eq(navcaster::core::normalize_access_group_uid(static_cast<const char *>(nullptr)), "default", "access policy null group normalize");
    expect_eq(navcaster::core::normalize_access_group_uid(std::string()), "default", "access policy empty group normalize");
    expect_true(navcaster::core::is_privileged_access_group("SYSTEM"), "access policy system privileged");
    expect_true(!navcaster::core::is_privileged_access_group("default"), "access policy default not privileged");
    expect_true(empty_access_policy.check_mount_visible("", "BASE01"), "access policy empty maps visible");
    expect_true(empty_access_policy.check_mount_access("default", "BASE01"), "access policy empty maps access");
    expect_true(empty_access_policy.check_mount_nearby("default", "BASE01"), "access policy empty maps nearby");
    expect_true(!empty_access_policy.check_nearest_mount_login("SYSTEM", "BASE01", &access_reason), "access policy empty nearest login");
    expect_eq(access_reason, "Access group policy not loaded", "access policy empty nearest reason");

    access_groups.emplace("ops", make_access_policy_group("ops", {
                                           {"nearest_mpt_enable", true},
                                           {"nearest_mpt_source_name", "NEAR1"},
                                           {"allow_visible_inside_group", false},
                                           {"allow_access_outside_group", false},
                                           {"allow_nearby_inside_group", false},
                                       }));
    access_groups.emplace("near-disabled", make_access_policy_group("near-disabled", {
                                                     {"nearest_mpt_enable", false},
                                                 }));
    access_sources.emplace("BASE_IN", make_access_policy_source("BASE_IN", "ops"));
    access_sources.emplace("CONFLICT", make_access_policy_source("CONFLICT", "ops"));
    access_decodes.emplace("DECODE_IN", make_access_policy_source("DECODE_IN", "ops"));
    access_decodes.emplace("CONFLICT", make_access_policy_source("CONFLICT", "decoded"));
    access_items["ops"].emplace("BASE_OVERRIDE", make_access_policy_item("BASE_OVERRIDE",
                                                                           caster::core::ACCESS_STATE_DEFALT,
                                                                           caster::core::ACCESS_STATE_ENABLE,
                                                                           caster::core::ACCESS_STATE_DEFALT));
    access_items["ops"].emplace("BASE_DENY", make_access_policy_item("BASE_DENY",
                                                                     caster::core::ACCESS_STATE_DISABLE,
                                                                     caster::core::ACCESS_STATE_DISABLE,
                                                                     caster::core::ACCESS_STATE_DISABLE));
    access_items["near-disabled"].emplace("BASE_NEAR", make_access_policy_item("BASE_NEAR",
                                                                               caster::core::ACCESS_STATE_DEFALT,
                                                                               caster::core::ACCESS_STATE_DEFALT,
                                                                               caster::core::ACCESS_STATE_ENABLE));
    navcaster::core::AccessPolicyService access_policy(access_sources, access_decodes, access_groups, access_items);
    expect_eq(access_policy.resolve_mount_group("BASE_IN"), "ops", "access policy source group resolve");
    expect_eq(access_policy.resolve_mount_group("DECODE_IN"), "ops", "access policy decode group resolve");
    expect_eq(access_policy.resolve_mount_group("CONFLICT"), "ops", "access policy source overrides decode group");
    expect_eq(access_policy.resolve_mount_group("NEAR1"), "ops", "access policy nearest group resolve");
    expect_eq(access_policy.resolve_mount_group("UNKNOWN"), "default", "access policy unknown group fallback");
    expect_true(access_policy.is_mount_inside_group("ops", "BASE_IN"), "access policy source inside group");
    expect_true(access_policy.is_mount_inside_group("ops", "BASE_OVERRIDE"), "access policy item inside group");
    access_reason.clear();
    expect_true(!access_policy.check_mount_visible("missing", "BASE_IN", &access_reason), "access policy missing group visible");
    expect_eq(access_reason, "Access group not found", "access policy missing group reason");
    access_reason.clear();
    expect_true(!access_policy.check_mount_visible("ops", "BASE_IN", &access_reason), "access policy inside visible deny");
    expect_eq(access_reason, "Mount point visible disabled inside group", "access policy inside visible reason");
    access_reason.clear();
    expect_true(!access_policy.check_mount_access("ops", "OUTSIDE", &access_reason), "access policy outside access deny");
    expect_eq(access_reason, "Mount point access disabled outside group", "access policy outside access reason");
    expect_true(access_policy.check_mount_access("ops", "BASE_OVERRIDE"), "access policy item access enable override");
    access_reason.clear();
    expect_true(!access_policy.check_mount_access("ops", "BASE_DENY", &access_reason), "access policy item access deny");
    expect_eq(access_reason, "Mount point access disabled by item policy", "access policy item access deny reason");
    access_reason.clear();
    expect_true(!access_policy.check_mount_visible("ops", "BASE_DENY", &access_reason), "access policy item visible deny");
    expect_eq(access_reason, "Mount point visible disabled by item policy", "access policy item visible deny reason");
    access_reason.clear();
    expect_true(!access_policy.check_mount_nearby("ops", "BASE_IN", &access_reason), "access policy nearby inside deny");
    expect_eq(access_reason, "Mount point nearby disabled inside group", "access policy nearby inside reason");
    access_reason.clear();
    expect_true(!access_policy.check_mount_nearby("near-disabled", "BASE_NEAR", &access_reason), "access policy nearby disabled before item");
    expect_eq(access_reason, "Nearest mount point disabled by group policy", "access policy nearby disabled reason");
    expect_true(access_policy.check_mount_visible("SYSTEM", "BASE_DENY"), "access policy system visible bypass");
    expect_true(access_policy.check_mount_access("SYSTEM", "BASE_DENY"), "access policy system access bypass");
    expect_true(access_policy.check_mount_nearby("SYSTEM", "BASE_NEAR"), "access policy system nearby bypass");
    expect_true(access_policy.check_nearest_mount_login("ops", "NEAR1"), "access policy nearest login configured");
    expect_true(access_policy.is_nearest_mount("NEAR1"), "access policy nearest mount lookup");
    expect_true(access_policy.check_nearest_mount_login("SYSTEM", "NEAR1"), "access policy system nearest login");
    access_reason.clear();
    expect_true(!access_policy.check_nearest_mount_login("ops", "OTHER", &access_reason), "access policy nearest login wrong mount");
    expect_eq(access_reason, "Nearest mount point not configured for group", "access policy nearest wrong mount reason");

    navcaster::core::SourceRecordMap table_records;
    navcaster::core::SourceRecordMap table_decodes;
    navcaster::core::AccessGroupMap table_groups;
    navcaster::core::AccessItemMap table_items;
    navcaster::core::AliasVisibleMap table_aliases;
    table_decodes.emplace("SRC1", make_access_policy_source("SRC1", "ops", {
                                         {"format_details", "DECODE"},
                                         {"nav_system", "GPS"},
                                         {"latitude", "31.00"},
                                         {"longitude", "121.00"},
                                     }));
    table_records.emplace("SRC1", make_access_policy_source("SRC1", "ops", {
                                         {"format_details", "MANUAL"},
                                         {"nav_system", "BDS"},
                                         {"latitude", "32.00"},
                                         {"longitude", "122.00"},
                                     }));
    table_records.emplace("OUTSIDE", make_access_policy_source("OUTSIDE", "other", {
                                           {"format_details", "OUT"},
                                       }));
    navcaster::core::SourceTableService open_table(table_records, table_decodes, table_groups, table_items, table_aliases);
    auto table_by_mount = source_table_by_mount(open_table.build_text(""));
    expect_true(table_by_mount.contains("SRC1"), "source table open includes source");
    expect_eq(table_by_mount["SRC1"][4], "MANUAL", "source table record overrides decode details");
    expect_eq(table_by_mount["SRC1"][6], "BDS", "source table record overrides decode nav");
    expect_eq(table_by_mount["SRC1"][9], "32.00", "source table record overrides latitude");
    expect_true(table_by_mount.contains("OUTSIDE"), "source table open includes all without policy");

    table_groups.emplace("ops", make_access_policy_group("ops", {
                                      {"nearest_mpt_enable", true},
                                      {"nearest_mpt_source_name", "NEAREST"},
                                      {"allow_visible_inside_group", true},
                                      {"allow_visible_outside_group", false},
                                  }));
    table_items["ops"].emplace("OUTSIDE", make_access_policy_item("OUTSIDE",
                                                                   caster::core::ACCESS_STATE_ENABLE,
                                                                   caster::core::ACCESS_STATE_DEFALT,
                                                                   caster::core::ACCESS_STATE_DEFALT));
    table_items["ops"].emplace("HIDDEN", make_access_policy_item("HIDDEN",
                                                                 caster::core::ACCESS_STATE_DISABLE,
                                                                 caster::core::ACCESS_STATE_DEFALT,
                                                                 caster::core::ACCESS_STATE_DEFALT));
    table_items["ops"].emplace("ALIAS_SRC1", make_access_policy_item("ALIAS_SRC1",
                                                                     caster::core::ACCESS_STATE_ENABLE,
                                                                     caster::core::ACCESS_STATE_DEFALT,
                                                                     caster::core::ACCESS_STATE_DEFALT));
    table_items["ops"].emplace("ALIAS_MISSING", make_access_policy_item("ALIAS_MISSING",
                                                                        caster::core::ACCESS_STATE_ENABLE,
                                                                        caster::core::ACCESS_STATE_DEFALT,
                                                                        caster::core::ACCESS_STATE_DEFALT));
    table_records.emplace("HIDDEN", make_access_policy_source("HIDDEN", "ops"));
    table_records.emplace("SRC_DUP_ALIAS", make_access_policy_source("ALIAS_DUP", "ops"));
    table_aliases["ALIAS_SRC1"] = "SRC1";
    table_aliases["ALIAS_MISSING"] = "MISSING";
    table_aliases["ALIAS_DUP"] = "SRC1";
    navcaster::core::SourceTableService policy_table(table_records, table_decodes, table_groups, table_items, table_aliases);
    table_by_mount = source_table_by_mount(policy_table.build_text("ops"));
    expect_true(table_by_mount.contains("SRC1"), "source table policy includes inside source");
    expect_true(table_by_mount.contains("OUTSIDE"), "source table policy item visible override");
    expect_true(!table_by_mount.contains("HIDDEN"), "source table policy item visible deny");
    expect_true(table_by_mount.contains("ALIAS_SRC1"), "source table includes visible alias");
    expect_eq(table_by_mount["ALIAS_SRC1"][4], "MANUAL", "source table alias keeps source fields");
    expect_true(!table_by_mount.contains("ALIAS_MISSING"), "source table skips missing alias source");
    expect_true(table_by_mount.contains("ALIAS_DUP"), "source table keeps original duplicate mount");
    expect_eq_int(static_cast<int>(table_by_mount.count("ALIAS_DUP")), 1, "source table duplicate alias not repeated");
    expect_true(table_by_mount.contains("NEAREST"), "source table appends nearest default");
    expect_eq(table_by_mount["NEAREST"][4], "1074(1),1084(1),1094(1),1124(1)", "source table nearest default details");
    expect_true(source_table_by_mount(policy_table.build_text("missing")).empty(), "source table missing group hidden");
    expect_true(source_table_by_mount(policy_table.build_text("SYSTEM")).contains("HIDDEN"), "source table system sees hidden source");

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
