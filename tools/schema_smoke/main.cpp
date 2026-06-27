#include "account_repository.h"
#include "account_controller.h"
#include "account_domain_repository.h"
#include "account_schema.h"
#include "audit_log_service.h"
#include "auth_session_service.h"
#include "auth_login_service.h"
#include "access_controller.h"
#include "access_policy_service.h"
#include "access_runtime_service.h"
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
#include "channel_lifecycle_service.h"
#include "core_callback_result.h"
#include "core_result.h"
#include "json_record.h"
#include "log_observability.h"
#include "mountpoint_subscriber_repository.h"
#include "mountpoint_subscriber_service.h"
#include "node_history_repository.h"
#include "node_history_service.h"
#include "node_log_level_service.h"
#include "operations_controller.h"
#include "redis_keys.h"
#include "node_history_recorder.h"
#include "redis_monitor_repository.h"
#include "redis_monitor_service.h"
#include "relay_controller.h"
#include "relay_repository.h"
#include "relay_scheduler.h"
#include "master_lease_service.h"
#include "ring_log_service.h"
#include "runtime_command_service.h"
#include "runtime_state_controller.h"
#include "runtime_state_repository.h"
#include "sourcetable_service.h"
#include "statistics_controller.h"
#include "source_controller.h"
#include "source_repository.h"
#include "statistics_service.h"
#include "sse_manager.h"
#include "sse_snapshot_service.h"
#include "status_service.h"
#include "system_event_service.h"
#include "self_service_controller.h"
#include "source_table_service.h"
#include "auth_record_limit.h"
#include "auth_session_record.h"
#include "ntrip_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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

pull_record make_pull_schedule_record(const std::string &uid, bool enabled, const std::string &revision = "")
{
    nlohmann::json body = {{"uid", uid}, {"enabled", enabled}};
    if (!revision.empty())
    {
        body["revision"] = revision;
    }
    pull_record record(uid);
    expect_eq_int(record.fromString(body.dump()), 0, "relay scheduler pull record parse " + uid);
    return record;
}

push_record make_push_schedule_record(const std::string &uid, bool enabled, const std::string &revision = "")
{
    nlohmann::json body = {{"uid", uid}, {"enabled", enabled}};
    if (!revision.empty())
    {
        body["revision"] = revision;
    }
    push_record record(uid);
    expect_eq_int(record.fromString(body.dump()), 0, "relay scheduler push record parse " + uid);
    return record;
}

pull_status make_pull_schedule_status(const std::string &uid)
{
    pull_status status(uid);
    status.set_node_info("node-a", "Node A");
    status.update_state("connect-" + uid, 1);
    return status;
}

push_status make_push_schedule_status(const std::string &uid)
{
    push_status status(uid);
    status.set_node_info("node-a", "Node A");
    status.update_state("connect-" + uid, 1);
    return status;
}

int current_process_id()
{
#if defined(_WIN32)
    return _getpid();
#else
    return getpid();
#endif
}
} // namespace

int main()
{
    using namespace navcaster;

    {
        auto ok = core::CoreResult::success("schema_smoke");
        expect_true(ok.ok(), "core result success ok");
        expect_eq_int(core::to_legacy_int(ok), 0, "core result success legacy int");
        expect_eq(std::string(core::core_error_code_name(core::CoreErrorCode::Ok)), "ok", "core result ok name");

        auto invalid = core::CoreResult::failure(core::CoreErrorCode::InvalidArgument,
                                                 "Register_Record",
                                                 "missing required argument")
                           .with_subject("connect_key")
                           .with_redis_key("MPT:REC:BASE01");
        expect_true(!invalid.ok(), "core result failure not ok");
        expect_eq_int(core::to_legacy_int(invalid), 1, "core result failure legacy int");
        expect_eq_int(core::to_legacy_int(invalid, -1), -1, "core result custom legacy int");
        expect_eq(std::string(core::core_error_code_name(invalid.code)), "invalid_argument", "core result invalid name");
        expect_true(invalid.summary().find("operation=Register_Record") != std::string::npos, "core result summary operation");
        expect_true(invalid.summary().find("subject=connect_key") != std::string::npos, "core result summary subject");
        expect_true(invalid.summary().find("redis_key=MPT:REC:BASE01") != std::string::npos, "core result summary redis key");

        core::CoreCallbackReply callback_reply(invalid);
        expect_true(callback_reply.reply.type == CasterReply::ERR, "core result callback error type");
        expect_eq(std::string(callback_reply.reply.str), "missing required argument", "core result callback message");
        expect_eq_int(static_cast<int>(callback_reply.reply.len), static_cast<int>(std::string("missing required argument").size()), "core result callback message length");

        auto publish_failed = core::CoreResult::failure(core::CoreErrorCode::PublishFailed,
                                                        "pub_base_channel",
                                                        "Redis publish command failed")
                                  .with_subject("connect-1")
                                  .with_redis_key("MPT:BASE01");
        expect_eq(std::string(core::core_error_code_name(publish_failed.code)), "publish_failed", "core result publish failed name");
        expect_eq_int(core::to_legacy_int(publish_failed, -1), -1, "core result publish failed redis legacy int");
    }

    {
        expect_eq(navcaster::observability::redact_header_line("Authorization: Basic abc"),
                  "Authorization: ***",
                  "observability redacts authorization header");
        expect_eq(navcaster::observability::redact_header_line("X-Trace: ok"),
                  "X-Trace: ok",
                  "observability keeps non-sensitive header");
        const auto source_summary = navcaster::observability::summarize_ntrip_request_line("SOURCE secret MOUNT1 HTTP/1.1");
        expect_true(source_summary.find("secret") == std::string::npos, "observability redacts SOURCE credential");
        expect_true(source_summary.find("mountpoint=MOUNT1") != std::string::npos, "observability keeps SOURCE mount");
        expect_eq(navcaster::observability::summarize_ntrip_request_line("GET /MOUNT2 HTTP/1.1"),
                  "method=GET target=/MOUNT2 version=HTTP/1.1",
                  "observability summarizes GET request line");
    }

    {
        using Lifecycle = core::ChannelLifecycleService;
        using CallbackMap = std::unordered_map<std::string, std::unordered_map<std::string, int>>;

        CallbackMap callback_map;
        expect_eq(Lifecycle::registration_bucket(core::ChannelEndpoint::Base, "BASE01", "ignored"), "BASE01", "channel lifecycle base bucket");
        expect_eq(Lifecycle::registration_bucket(core::ChannelEndpoint::Rover, "MOUNT01", "user01"), "user01", "channel lifecycle rover bucket");
        expect_eq(Lifecycle::subscription_bucket("BASE01"), "BASE01", "channel lifecycle subscription bucket");
        expect_eq(Lifecycle::connection_redis_key(core::ChannelEndpoint::Base, "BASE01"), "MPT:REC:BASE01", "channel lifecycle base rec key");
        expect_eq(Lifecycle::connection_redis_key(core::ChannelEndpoint::Rover, "user01"), "USR:REC:user01", "channel lifecycle rover rec key");
        expect_eq(Lifecycle::subscription_redis_key(core::ChannelEndpoint::Base, "BASE01"), "MPT:SUB:BASE01", "channel lifecycle base sub key");
        expect_eq(Lifecycle::publish_redis_key(core::ChannelEndpoint::Rover, "user01"), "USR:user01", "channel lifecycle rover publish key");

        auto identity_missing = Lifecycle::require_channel_identity("sub_base_channel", "", "connect-1");
        expect_true(!identity_missing.ok(), "channel lifecycle rejects empty channel");
        expect_eq(std::string(core::core_error_code_name(identity_missing.code)), "invalid_argument", "channel lifecycle empty channel code");
        expect_eq(identity_missing.subject, "channel", "channel lifecycle empty channel subject");

        auto create_plan = Lifecycle::ensure_bucket(callback_map, "register_base_channel", "BASE01", Lifecycle::connection_redis_key(core::ChannelEndpoint::Base, "BASE01"));
        expect_true(create_plan.created, "channel lifecycle creates bucket");
        expect_eq(create_plan.bucket, "BASE01", "channel lifecycle create bucket name");
        auto reuse_plan = Lifecycle::ensure_bucket(callback_map, "register_base_channel", "BASE01", Lifecycle::connection_redis_key(core::ChannelEndpoint::Base, "BASE01"));
        expect_true(!reuse_plan.created, "channel lifecycle reuses bucket");

        auto absent = Lifecycle::require_connect_absent(callback_map, "register_base_channel", "BASE01", "connect-1", "MPT:REC:BASE01");
        expect_true(absent.ok(), "channel lifecycle connect absent ok");
        callback_map["BASE01"]["connect-1"] = 1;
        auto duplicate = Lifecycle::require_connect_absent(callback_map, "register_base_channel", "BASE01", "connect-1", "MPT:REC:BASE01");
        expect_true(!duplicate.ok(), "channel lifecycle duplicate rejected");
        expect_eq(std::string(core::core_error_code_name(duplicate.code)), "state_conflict", "channel lifecycle duplicate code");
        expect_eq(duplicate.redis_key, "MPT:REC:BASE01", "channel lifecycle duplicate key");

        auto present = Lifecycle::require_connect_present(callback_map, "withdraw_base_channel", "BASE01", "connect-1", "MPT:REC:BASE01");
        expect_true(present.ok(), "channel lifecycle connect present ok");
        auto missing_connect = Lifecycle::require_connect_present(callback_map, "withdraw_base_channel", "BASE01", "connect-2", "MPT:REC:BASE01");
        expect_true(!missing_connect.ok(), "channel lifecycle missing connect rejected");
        expect_eq(std::string(core::core_error_code_name(missing_connect.code)), "not_found", "channel lifecycle missing connect code");
        expect_eq(missing_connect.subject, "connect-2", "channel lifecycle missing connect subject");
    }

    {
        const auto auth_conf_path = std::filesystem::temp_directory_path() / ("navcaster_schema_auth_verify_" + std::to_string(current_process_id()) + ".yml");
        std::ofstream auth_conf(auth_conf_path);
        auth_conf
            << "Base_Setting:\n"
            << "  Anonymous_Login: true\n"
            << "  Online_Protection: false\n"
            << "Rover_Setting:\n"
            << "  Anonymous_Login: false\n"
            << "  Online_Protection: true\n"
            << "Source_Setting:\n"
            << "  Anonymous_Login: false\n"
            << "Reids_Connect_Setting:\n"
            << "  IP: 127.0.0.1\n"
            << "  Port: 16379\n"
            << "  Requirepass: password\n";
        auth_conf.close();

        auto *config = ntrip_config::getInstance();
        expect_eq_int(config->load_Auth_Conf(auth_conf_path.string()), 0, "auth config load");
        expect_true(config->_auth_verify_opt.base_anonymous_login(), "auth config base anonymous");
        expect_true(!config->_auth_verify_opt.base_online_protection(), "auth config base online protection");
        expect_true(!config->_auth_verify_opt.rover_anonymous_login(), "auth config rover anonymous preserved");
        expect_true(config->_auth_verify_opt.rover_online_protection(), "auth config rover online protection");
        expect_true(!config->_auth_verify_opt.source_anonymous_login(), "auth config source anonymous");
        expect_eq(config->_auth_verify_opt.redis_host(), "127.0.0.1", "auth config redis host");
        expect_eq_int(config->_auth_verify_opt.redis_port(), 16379, "auth config redis port");
        expect_eq(config->_auth_verify_opt.redis_password(), "password", "auth config redis password");

        std::ofstream reverse_auth_conf(auth_conf_path);
        reverse_auth_conf
            << "Base_Setting:\n"
            << "  Anonymous_Login: false\n"
            << "  Online_Protection: true\n"
            << "Rover_Setting:\n"
            << "  Anonymous_Login: true\n"
            << "  Online_Protection: false\n"
            << "Source_Setting:\n"
            << "  Anonymous_Login: true\n"
            << "Reids_Connect_Setting:\n"
            << "  IP: 127.0.0.2\n"
            << "  Port: 26379\n"
            << "  Requirepass: other\n";
        reverse_auth_conf.close();

        expect_eq_int(config->load_Auth_Conf(auth_conf_path.string()), 0, "auth config reload");
        expect_true(config->_auth_verify_opt.rover_anonymous_login(), "auth config rover anonymous true preserved");
        expect_true(!config->_auth_verify_opt.rover_online_protection(), "auth config rover online protection false preserved");
        std::filesystem::remove(auth_conf_path);
    }

    {
        std::multimap<std::time_t, std::string> records = {
            {100, "old"},
            {200, "middle"},
            {300, "current"}};
        auto decision = auth::plan_record_limit(records, "current", 2, true);
        expect_true(!decision.current_allowed, "auth online protection rejects newest current");
        expect_eq_int(static_cast<int>(decision.evicted_connect_keys.size()), 0, "auth online protection no old eviction");

        decision = auth::plan_record_limit(records, "current", 2, false);
        expect_true(decision.current_allowed, "auth online protection disabled allows current");
        expect_eq_int(static_cast<int>(decision.evicted_connect_keys.size()), 1, "auth online protection disabled evicts one");
        expect_eq(decision.evicted_connect_keys[0], "old", "auth online protection disabled evicts oldest");

        std::multimap<std::time_t, std::string> same_second_records = {
            {100, "current"},
            {100, "old-a"},
            {100, "old-b"}};
        decision = auth::plan_record_limit(same_second_records, "current", 2, true);
        expect_true(!decision.current_allowed, "auth online protection same second rejects current");
        expect_eq_int(static_cast<int>(decision.evicted_connect_keys.size()), 0, "auth online protection same second keeps old records");

        decision = auth::plan_record_limit(same_second_records, "current", 2, false);
        expect_true(decision.current_allowed, "auth online protection disabled same second allows current");
        expect_eq_int(static_cast<int>(decision.evicted_connect_keys.size()), 1, "auth online protection disabled same second evicts one");
        expect_eq(decision.evicted_connect_keys[0], "old-a", "auth online protection disabled same second evicts non current");

        decision = auth::plan_record_limit(same_second_records, "current", 1, false);
        expect_true(decision.current_allowed, "auth online protection disabled limit one keeps current");
        expect_eq_int(static_cast<int>(decision.evicted_connect_keys.size()), 2, "auth online protection disabled limit one evicts others");

        decision = auth::plan_record_limit(same_second_records, "current", 0, false);
        expect_true(!decision.current_allowed, "auth online protection zero limit rejects current");
        expect_eq_int(static_cast<int>(decision.evicted_connect_keys.size()), 0, "auth online protection zero limit no eviction");
    }

    {
        auth::AuthLoginOptions options;
        options.server_anonymous_login = true;
        options.client_anonymous_login = false;
        options.source_anonymous_login = true;
        options.server_online_protection = false;
        options.client_online_protection = true;
        expect_true(auth::AuthLoginService::anonymous_enabled(AuthType::SERVER, options), "auth login service server anonymous");
        expect_true(!auth::AuthLoginService::anonymous_enabled(AuthType::CLIENT, options), "auth login service client named");
        expect_true(auth::AuthLoginService::anonymous_enabled(AuthType::SOURCE, options), "auth login service source anonymous");
        expect_true(auth::AuthLoginService::online_protection_enabled(AuthType::CLIENT, options), "auth login service client protection");
        expect_true(!auth::AuthLoginService::online_protection_enabled(AuthType::SOURCE, options), "auth login service source no protection");
        expect_eq(std::string(auth::AuthLoginService::stage_name(auth::AuthLoginStage::PasswordRejected)), "password_rejected", "auth login service stage name");

        nlohmann::json active_account = {
            {"account", "login-user"},
            {"password_hash", account_schema::make_password_hash("secret", "abcd", 2)},
            {"password_algo", account_schema::PASSWORD_ALGO_PBKDF2_SHA256},
            {"password_salt", "abcd"},
            {"password_iterations", 2},
            {"state", 1},
            {"active", 1},
            {"connection_limit", 2},
            {"group_uid", "ops"}
        };
        auto account_decision = auth::AuthLoginService::evaluate_account(active_account.dump(), "secret", AuthType::CLIENT, 1000);
        expect_true(account_decision.result.ok(), "auth login service accepts hashed account");
        expect_eq(std::string(auth::AuthLoginService::stage_name(account_decision.stage)), "accepted", "auth login service accept stage");
        expect_eq(account_decision.group_uid, "ops", "auth login service group");
        expect_eq_int(account_decision.connect_limit, 2, "auth login service connect limit");

        auto missing_account = auth::AuthLoginService::account_not_found();
        expect_true(!missing_account.result.ok(), "auth login service rejects missing account");
        expect_eq(std::string(core::core_error_code_name(missing_account.result.code)), "not_found", "auth login service missing account code");
        expect_eq(missing_account.result.message, "account_not_found", "auth login service missing account reason");
        expect_eq(missing_account.legacy_reply, "User Not active or existed!", "auth login service missing account legacy reply");

        auto wrong_password = auth::AuthLoginService::evaluate_account(active_account.dump(), "wrong", AuthType::CLIENT, 1000);
        expect_true(!wrong_password.result.ok(), "auth login service rejects bad password");
        expect_eq(std::string(core::core_error_code_name(wrong_password.result.code)), "permission_denied", "auth login service password code");
        expect_eq(wrong_password.result.message, "password_mismatch", "auth login service password reason");
        expect_eq(wrong_password.legacy_reply, "User Password Error!", "auth login service password legacy reply");

        active_account["active"] = 2;
        auto disabled = auth::AuthLoginService::evaluate_account(active_account.dump(), "secret", AuthType::CLIENT, 1000);
        expect_true(!disabled.result.ok(), "auth login service rejects disabled account");
        expect_eq(std::string(core::core_error_code_name(disabled.result.code)), "permission_denied", "auth login service disabled code");
        expect_eq(disabled.result.message, "account_disabled", "auth login service disabled reason code");
        expect_eq(disabled.legacy_reply, "account is not active", "auth login service disabled reason");

        auto malformed = auth::AuthLoginService::evaluate_account("{broken-json", "secret", AuthType::CLIENT, 1000);
        expect_true(!malformed.result.ok(), "auth login service rejects malformed account");
        expect_eq(std::string(core::core_error_code_name(malformed.result.code)), "parse_error", "auth login service parse code");
        expect_eq(malformed.legacy_reply, "User auth info invalid!", "auth login service parse legacy reply");

        std::multimap<std::time_t, std::string> auth_records = {
            {100, "old"},
            {200, "current"}
        };
        auto limit_plan = auth::AuthLoginService::evaluate_record_limit(auth_records, "current", 1, false, "login-user");
        expect_true(limit_plan.current_allowed, "auth login service kick-old allows current");
        expect_eq_int(static_cast<int>(limit_plan.evicted_connect_keys.size()), 1, "auth login service kick-old evicts one");
        expect_eq(limit_plan.evicted_connect_keys[0], "old", "auth login service kick-old evicts oldest");

        limit_plan = auth::AuthLoginService::evaluate_record_limit(auth_records, "current", 1, true, "login-user");
        expect_true(!limit_plan.current_allowed, "auth login service reject-new rejects current");
        expect_eq(std::string(core::core_error_code_name(limit_plan.result.code)), "state_conflict", "auth login service record limit code");
        expect_eq(limit_plan.result.message, "record_limit_exceeded", "auth login service record limit reason");
        expect_eq(limit_plan.legacy_reply, "User Connects Upper Limit , kick out this Connect!", "auth login service record limit legacy reply");
    }

    {
        expect_eq(auth::active_session_key("alice"), "ACT:SESSION:alice", "auth active session key");
        expect_eq(std::string(auth::auth_type_name(AuthType::SERVER)), "server", "auth active session server type");
        expect_eq(std::string(auth::auth_type_name(AuthType::CLIENT)), "client", "auth active session client type");
        expect_eq(std::string(auth::auth_type_name(AuthType::SOURCE)), "source", "auth active session source type");
        expect_eq(std::string(auth::auth_type_name(AuthType::UNKNOWN)), "unknown", "auth active session unknown type");

        const auto session = nlohmann::json::parse(auth::build_active_session_record_json(
            "alice",
            "conn-1",
            AuthType::CLIENT,
            100,
            105,
            ""));
        expect_eq(session.value("uid", ""), "conn-1", "auth active session uid");
        expect_eq(session.value("connect_key", ""), "conn-1", "auth active session connect key");
        expect_eq(session.value("account", ""), "alice", "auth active session account");
        expect_true(!session.value("anonymous", true), "auth active session registered flag");
        expect_eq(session.value("auth_type", ""), "client", "auth active session auth type");
        expect_eq_int(session.value("online_time", 0), 100, "auth active session online time");
        expect_eq_int(session.value("update_time", 0), 105, "auth active session update time");
        expect_eq(session.value("addr", "x"), "", "auth active session empty addr");
        expect_eq(session.value("port", "x"), "", "auth active session empty port");
        expect_eq(session.value("group_uid", ""), "default", "auth active session default group");

        const auto grouped_session = nlohmann::json::parse(auth::build_active_session_record_json(
            "base",
            "conn-2",
            AuthType::SERVER,
            200,
            240,
            "ops"));
        expect_eq(grouped_session.value("auth_type", ""), "server", "auth active session grouped server type");
        expect_eq(grouped_session.value("group_uid", ""), "ops", "auth active session explicit group");
    }

    {
        using namespace navcaster::core;
        const nlohmann::json auth_index = {
            {"access_account_id", "aacc-runtime"},
            {"owner_account_id", "acc-runtime"},
            {"access_username", "runtime-rover"},
            {"access_kind", "user_client"},
            {"access_status", "active"},
            {"owner_status", "active"},
            {"mount_point_group_id", "mpg-runtime"},
            {"balance_cents", 500},
            {"expire_time", 0},
        };
        auto runtime_check = validate_access_auth_index(auth_index, "client", "RUNTIME", 1782432000, true);
        expect_true(runtime_check.ok, "access runtime validates active user client");
        runtime_check = validate_access_auth_index(auth_index, "server", "RUNTIME", 1782432000, true);
        expect_true(!runtime_check.ok && runtime_check.reason == "access_kind_not_allowed", "access runtime rejects user client as station");
        auto disabled_auth_index = auth_index;
        disabled_auth_index["access_status"] = "disabled";
        runtime_check = validate_access_auth_index(disabled_auth_index, "client", "RUNTIME", 1782432000, true);
        expect_true(!runtime_check.ok && runtime_check.reason == "access_account_disabled", "access runtime rejects disabled access");

        runtime_check = validate_access_dependencies({{"status", "active"}},
                                                     {{"status", "active"}},
                                                     {{"status", "active"}},
                                                     {{"status", "active"}});
        expect_true(runtime_check.ok, "access runtime validates active dependencies");
        runtime_check = validate_access_dependencies({{"status", "active"}},
                                                     {{"status", "active"}},
                                                     {{"status", "disabled"}},
                                                     {{"status", "active"}});
        expect_true(!runtime_check.ok && runtime_check.reason == "mountpoint_not_in_group", "access runtime rejects disabled member");

        expect_eq(runtime_period_from_unix(1782432000), "202606", "access runtime billing period");
        expect_eq_int(static_cast<int>(calculate_runtime_cost_cents(1800, 120, 1.5)), 90, "access runtime cost rounding");
        runtime_check = revalidate_access_runtime_session({auth_index, "client", "RUNTIME", 1782432000, 60, 1200, 1.0});
        expect_true(runtime_check.ok, "access runtime revalidation accepts funded user client");
        runtime_check = revalidate_access_runtime_session({disabled_auth_index, "client", "RUNTIME", 1782432000, 60, 1200, 1.0});
        expect_true(!runtime_check.ok && runtime_check.reason == "access_account_disabled", "access runtime revalidation rejects disabled access");
        auto expired_auth_index = auth_index;
        expired_auth_index["expire_time"] = 1782431999;
        runtime_check = revalidate_access_runtime_session({expired_auth_index, "client", "RUNTIME", 1782432000, 60, 1200, 1.0});
        expect_true(!runtime_check.ok && runtime_check.reason == "access_account_expired", "access runtime revalidation rejects expired access");
        auto low_balance_auth_index = auth_index;
        low_balance_auth_index["balance_cents"] = 1;
        runtime_check = revalidate_access_runtime_session({low_balance_auth_index, "client", "RUNTIME", 1782432000, 60, 1200, 1.0});
        expect_true(!runtime_check.ok && runtime_check.reason == "balance_insufficient", "access runtime revalidation rejects next slice insufficient balance");
        auto supplier_auth_index = auth_index;
        supplier_auth_index["access_kind"] = "supplier_station";
        supplier_auth_index["balance_cents"] = 0;
        runtime_check = revalidate_access_runtime_session({supplier_auth_index, "server", "RUNTIME", 1782432000, 60, 1200, 1.0});
        expect_true(runtime_check.ok, "access runtime revalidation does not debit supplier station");
        runtime_check = revalidate_access_runtime_session({low_balance_auth_index, "client", "RUNTIME", 1782432000, 60, 1200, 1.0, ACCESS_RUNTIME_BILLING_MODE_SUBSCRIPTION});
        expect_true(runtime_check.ok, "access runtime subscription revalidation skips balance precheck");

        const nlohmann::json subscription_records = {
            {"sub-late", {
                {"subscription_id", "sub-late"},
                {"account_id", "acc-runtime"},
                {"group_ids", nlohmann::json::array({"mpg-other"})},
                {"status", "active"},
                {"start_time", 1782431000},
                {"expire_time", 1782433000},
            }},
            {"sub-runtime", {
                {"subscription_id", "sub-runtime"},
                {"account_id", "acc-runtime"},
                {"group_ids", nlohmann::json::array({"mpg-runtime"})},
                {"status", "active"},
                {"start_time", 1782431000},
                {"expire_time", 1782432500},
            }},
        };
        const auto selected_subscription = select_runtime_subscription(subscription_records, "mpg-runtime", 1782432000);
        expect_eq(selected_subscription.value("subscription_id", std::string{}), "sub-runtime", "access runtime selects covering subscription");
        runtime_check = validate_runtime_subscription(selected_subscription, "mpg-runtime", 1782432000);
        expect_true(runtime_check.ok, "access runtime validates active subscription");
        runtime_check = validate_runtime_subscription(selected_subscription, "mpg-runtime", 1782432600);
        expect_true(!runtime_check.ok && runtime_check.reason == "subscription_expired", "access runtime rejects expired subscription");
        auto revoked_subscription = selected_subscription;
        revoked_subscription["status"] = "disabled";
        runtime_check = validate_runtime_subscription(revoked_subscription, "mpg-runtime", 1782432000);
        expect_true(!runtime_check.ok && runtime_check.reason == "subscription_revoked", "access runtime rejects disabled subscription");

        AccessRuntimeRecordInput runtime_input;
        runtime_input.owner_account_id = "acc-runtime";
        runtime_input.access_account_id = "aacc-runtime";
        runtime_input.access_username = "runtime-rover";
        runtime_input.access_kind = "user_client";
        runtime_input.mountpoint = "RUNTIME";
        runtime_input.group_id = "mpg-runtime";
        runtime_input.connect_key = "conn-runtime";
        runtime_input.auth_type = "client";
        runtime_input.addr = "127.0.0.1";
        runtime_input.port = 2101;
        runtime_input.start_time = 1782432000;
        runtime_input.update_time = 1782432060;
        runtime_input.end_time = 1782432060;
        runtime_input.used_seconds = 60;
        runtime_input.stat_cost_cents = 2;
        runtime_input.actual_debit_cents = 2;
        runtime_input.balance_after_cents = 498;
        runtime_input.hourly_price_cents = 120;
        runtime_input.billing_multiplier = 1.0;
        runtime_input.disconnect_reason = "client_closed";
        const auto online_session = build_online_session_record(runtime_input);
        expect_eq(online_session.value("access_account_id", std::string{}), "aacc-runtime", "access runtime online session access id");
        const auto billing = build_billing_usage_entry(runtime_input);
        expect_eq(billing.value("account_id", std::string{}), "acc-runtime", "access runtime billing owner");
        expect_eq_int(billing.value("actual_debit_cents", 0), 2, "access runtime billing debit");
        const auto ledger = build_balance_ledger_entry(runtime_input);
        expect_eq_int(ledger.value("delta_cents", 0), -2, "access runtime ledger delta");
        runtime_input.billing_mode = ACCESS_RUNTIME_BILLING_MODE_SUBSCRIPTION;
        runtime_input.subscription_id = "sub-runtime";
        runtime_input.subscription_snapshot = selected_subscription;
        runtime_input.actual_debit_cents = 0;
        const auto subscription_billing = build_billing_usage_entry(runtime_input);
        expect_eq(subscription_billing.value("subscription_id", std::string{}), "sub-runtime", "access runtime billing stores subscription id");
        expect_eq_int(subscription_billing.value("actual_debit_cents", -1), 0, "access runtime subscription billing does not debit");

        runtime_input.access_kind = "supplier_station";
        runtime_input.auth_type = "server";
        runtime_input.billing_mode = ACCESS_RUNTIME_BILLING_MODE_PAYG;
        runtime_input.subscription_id.clear();
        runtime_input.subscription_snapshot = nlohmann::json::object();
        runtime_input.earning_cents = runtime_input.stat_cost_cents;
        const auto supply = build_supplier_supply_usage(runtime_input);
        expect_eq(supply.value("supplier_account_id", std::string{}), "acc-runtime", "access runtime supply owner");
        expect_eq_int(supply.value("earning_cents", 0), 2, "access runtime supply earning");
        const auto station = build_station_record(runtime_input, true);
        expect_true(station.value("current_online", false), "access runtime station online");
        const auto station_event = build_station_event(runtime_input, "login");
        expect_eq(station_event.value("event_type", std::string{}), "login", "access runtime station login event");
    }

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

    auto active_sessions = account_repo.list_active_sessions();
    expect_true(active_sessions.is_object() && active_sessions.empty(), "repository active sessions empty object");
    fake_redis.hset(navcaster::redis_keys::ACT_ACTIVE, "login-index-only", nlohmann::json{{"account", "login-index-only"}}.dump());
    active_sessions = account_repo.list_active_sessions();
    expect_true(!active_sessions.contains("login-index-only"), "repository active sessions ignore ACT_ACTIVE");

    {
        FakeRedisHashClient only_legacy_redis;
        navcaster::storage::AccountRepository only_legacy_repo(only_legacy_redis);
        only_legacy_redis.hset(navcaster::redis_keys::STR_ACTIVE_LEGACY, "legacy-only", nlohmann::json{{"uid", "legacy-only"}, {"account", "legacy-user"}, {"password", "legacy-secret"}}.dump());
        auto only_legacy_sessions = only_legacy_repo.list_active_sessions();
        expect_true(only_legacy_sessions.contains("legacy-only"), "repository active sessions read only STR_ACTIVE");
        expect_eq(only_legacy_sessions["legacy-only"].value("account", std::string{}), "legacy-user", "repository only legacy account");
        expect_missing(only_legacy_sessions["legacy-only"], "password", "repository only legacy strips password");
    }

    {
        FakeRedisHashClient only_session_redis;
        navcaster::storage::AccountRepository only_session_repo(only_session_redis);
        only_session_redis.hset(navcaster::redis_keys::act_session("active-user").c_str(), "session-only", nlohmann::json{{"uid", "session-only"}, {"connect_key", "session-only"}, {"account", "active-user"}, {"auth_type", "client"}, {"online_time", 10}, {"update_time", 20}, {"password_hash", "hidden"}}.dump());
        auto only_session_sessions = only_session_repo.list_active_sessions();
        expect_true(only_session_sessions.contains("session-only"), "repository active sessions read only ACT_SESSION");
        expect_eq(only_session_sessions["session-only"].value("connect_key", std::string{}), "session-only", "repository only session connect key");
        expect_missing(only_session_sessions["session-only"], "password_hash", "repository only session strips hash");
    }

    fake_redis.hset(navcaster::redis_keys::STR_ACTIVE_LEGACY, "legacy-1", nlohmann::json{{"uid", "legacy-1"}, {"account", "legacy-user"}}.dump());
    fake_redis.hset(navcaster::redis_keys::STR_ACTIVE_LEGACY, "conflict", nlohmann::json{{"uid", "conflict"}, {"account", "legacy-user"}, {"password", "legacy-secret"}}.dump());
    fake_redis.hset(navcaster::redis_keys::act_session("active-user").c_str(), "session-1", nlohmann::json{{"uid", "session-1"}, {"connect_key", "session-1"}, {"account", "active-user"}, {"auth_type", "client"}, {"online_time", 10}, {"update_time", 20}, {"password_hash", "hidden"}}.dump());
    fake_redis.hset(navcaster::redis_keys::act_session("active-user").c_str(), "session-2", nlohmann::json{{"uid", "session-2"}, {"connect_key", "session-2"}, {"account", "active-user"}, {"auth_type", "server"}}.dump());
    fake_redis.hset(navcaster::redis_keys::act_session("legacy-user").c_str(), "conflict", nlohmann::json{{"uid", "conflict"}, {"connect_key", "conflict"}, {"account", "active-user"}, {"auth_type", "client"}, {"password_salt", "hidden"}}.dump());
    active_sessions = account_repo.list_active_sessions();
    expect_true(active_sessions.contains("legacy-1"), "repository active sessions keep legacy fallback");
    expect_true(active_sessions.contains("session-1"), "repository active sessions read ACT_SESSION");
    expect_true(active_sessions.contains("session-2"), "repository active sessions preserve multi connect same account");
    expect_eq(active_sessions["session-1"].value("connect_key", std::string{}), "session-1", "repository active sessions connect key");
    expect_eq(active_sessions["conflict"].value("account", std::string{}), "active-user", "repository active sessions prefer ACT_SESSION over legacy");
    expect_missing(active_sessions["conflict"], "password", "repository active sessions strips legacy password");
    expect_missing(active_sessions["conflict"], "password_salt", "repository active sessions strips session salt");
    expect_missing(active_sessions["session-1"], "password_hash", "repository active sessions strips session hash");

    {
        FakeRedisHashClient domain_redis;
        navcaster::storage::AccountDomainRepository domain_repo(domain_redis);

        auto domain_result = domain_repo.create_account({
            {"account_id", "acc-user"},
            {"username", "customer-a"},
            {"role", "user"},
            {"status", "active"},
            {"balance_cents", 10000},
            {"concurrency_limit", 3},
        }, 5000);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain account create user ok");
        expect_eq(domain_result.id, "acc-user", "domain account id");
        expect_true(domain_redis.hget(navcaster::redis_keys::ACC_RECORD, "acc-user").is_object(), "domain account writes ACC:RECORD");
        expect_eq(domain_redis.hget(navcaster::redis_keys::ACC_USERNAME, "customer-a").value("id", std::string{}), "acc-user", "domain account username index");
        expect_true(domain_repo.create_account({{"account_id", "acc-bad"}, {"username", "bad"}, {"role", "operator"}}, 5001).status == navcaster::storage::RepositoryStatus::Invalid, "domain rejects invalid role");
        expect_true(domain_repo.create_account({{"account_id", "acc-other"}, {"username", "customer-a"}, {"role", "user"}}, 5001).status == navcaster::storage::RepositoryStatus::Conflict, "domain account username unique");
        expect_true(domain_repo.create_account({{"account_id", "acc-user"}, {"username", "new-failed-name"}, {"role", "user"}}, 5001).status == navcaster::storage::RepositoryStatus::Conflict, "domain duplicate account id rejected");
        expect_true(domain_redis.hget(navcaster::redis_keys::ACC_USERNAME, "new-failed-name").is_null(), "domain failed account create does not tombstone username");

        domain_result = domain_repo.create_account({
            {"account_id", "acc-supplier"},
            {"username", "supplier-a"},
            {"role", "supplier"},
            {"status", "active"},
            {"concurrency_limit", 2},
        }, 5002);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain supplier account create ok");

        domain_result = domain_repo.create_account({
            {"account_id", "acc-admin"},
            {"username", "admin-a"},
            {"role", "admin"},
            {"status", "active"},
        }, 5003);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain admin account create ok");

        expect_true(domain_repo.create_mount_point_group({
            {"group_id", "mpg-basic"},
            {"name", "Basic"},
            {"billing_multiplier", 1.25},
        }, 5010).status == navcaster::storage::RepositoryStatus::Ok, "domain mount point group create");
        expect_true(domain_repo.create_mount_point({{"mountpoint", "BASE01"}, {"hourly_price_cents", 120}}, 5011).status == navcaster::storage::RepositoryStatus::Ok, "domain mount point create");
        expect_true(domain_repo.add_mount_point_group_member("mpg-basic", {{"mountpoint", "BASE01"}}, 5012).status == navcaster::storage::RepositoryStatus::Ok, "domain group member create");
        expect_true(domain_redis.hget(navcaster::redis_keys::mpgrp_member("mpg-basic").c_str(), "BASE01").is_object(), "domain group member key");
        expect_true(domain_redis.hget(navcaster::redis_keys::ACCESS_GROUP, "mpg-basic").is_object(), "domain mount point group syncs legacy access group");
        expect_eq_int(domain_redis.hget(navcaster::redis_keys::access_item("mpg-basic").c_str(), "BASE01").value("allow_access", 0), 1, "domain mount point member syncs legacy access item");
        expect_true(domain_repo.grant_account_group("acc-user", {{"group_id", "mpg-basic"}}, 5013).status == navcaster::storage::RepositoryStatus::Ok, "domain user group grant");
        expect_true(domain_repo.grant_account_group("acc-supplier", {{"group_id", "mpg-basic"}}, 5014).status == navcaster::storage::RepositoryStatus::Ok, "domain supplier group grant");
        expect_true(domain_repo.grant_account_group("acc-admin", {{"group_id", "mpg-basic"}}, 5015).status == navcaster::storage::RepositoryStatus::Ok, "domain admin group grant");
        expect_true(domain_repo.create_subscription_plan({
            {"plan_id", "plan-basic"},
            {"name", "Basic Plan"},
            {"group_ids", nlohmann::json::array({"mpg-basic"})},
            {"price_cents", 9900},
            {"duration_days", 30},
        }, 5016).status == navcaster::storage::RepositoryStatus::Ok, "domain subscription plan create");
        expect_true(domain_redis.hget(navcaster::redis_keys::SUB_PLAN, "plan-basic").is_object(), "domain subscription plan key");
        expect_true(domain_repo.create_subscription_plan({
            {"plan_id", "plan-unknown-group"},
            {"name", "Bad Plan"},
            {"group_ids", nlohmann::json::array({"missing-group"})},
        }, 5017).status == navcaster::storage::RepositoryStatus::Invalid, "domain subscription plan rejects unknown group");
        auto plan_update = domain_repo.update_subscription_plan("plan-basic", {{"price_cents", 12900}, {"duration_days", 45}}, 5018);
        expect_true(plan_update.status == navcaster::storage::RepositoryStatus::Ok, "domain subscription plan update");
        expect_eq_int(plan_update.record.value("price_cents", 0), 12900, "domain subscription plan price updates");

        domain_result = domain_repo.create_access_account({
            {"access_account_id", "aacc-user-1"},
            {"owner_account_id", "acc-user"},
            {"username", "rover-user"},
            {"kind", "user_client"},
            {"status", "active"},
            {"password", "rover-pass"},
            {"mount_point_group_id", "mpg-basic"},
            {"concurrency_limit", 1},
        }, 5020);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain user access account create ok");
        expect_true(domain_redis.hget(navcaster::redis_keys::AACC_RECORD, "aacc-user-1").is_object(), "domain access account writes AACC:RECORD");
        expect_eq(domain_redis.hget(navcaster::redis_keys::AACC_USERNAME, "rover-user").value("id", std::string{}), "aacc-user-1", "domain access username index");
        auto active_access = domain_redis.hget(navcaster::redis_keys::AACC_ACTIVE, "rover-user");
        expect_eq(active_access.value("owner_account_id", std::string{}), "acc-user", "domain access active owner");
        expect_eq(active_access.value("owner_role", std::string{}), "user", "domain access active owner role");
        expect_eq(active_access.value("account", std::string{}), "rover-user", "domain access active auth account");
        expect_eq(active_access.value("group_uid", std::string{}), "mpg-basic", "domain access active auth group");
        expect_eq_int(active_access.value("connection_limit", 0), 1, "domain access active auth connection limit");
        expect_has(active_access, "password_hash", "domain access active carries password hash");
        expect_missing(active_access, "password", "domain access active hides plaintext password");
        auto active_login = navcaster::auth::AuthLoginService::evaluate_account(active_access.dump(), "rover-pass", AuthType::CLIENT, 5020);
        expect_true(active_login.result.ok(), "domain access active index works with auth login service");
        expect_eq(active_login.group_uid, "mpg-basic", "domain access active index login group");
        expect_true(domain_redis.hget(navcaster::redis_keys::aacc_owner("acc-user").c_str(), "aacc-user-1").is_object(), "domain owner access summary");

        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-missing-password"},
            {"owner_account_id", "acc-user"},
            {"username", "missing-password"},
            {"kind", "user_client"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5021).status == navcaster::storage::RepositoryStatus::Invalid, "domain access account requires password");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-user-bad-kind"},
            {"owner_account_id", "acc-user"},
            {"username", "bad-station"},
            {"kind", "supplier_station"},
            {"password", "bad-pass"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5021).status == navcaster::storage::RepositoryStatus::Invalid, "domain user cannot create supplier station");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-no-grant"},
            {"owner_account_id", "acc-user"},
            {"username", "no-grant"},
            {"kind", "user_client"},
            {"password", "no-grant-pass"},
            {"mount_point_group_id", "mpg-missing"},
        }, 5022).status == navcaster::storage::RepositoryStatus::Invalid, "domain access account requires granted group");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-over-limit"},
            {"owner_account_id", "acc-user"},
            {"username", "over-limit"},
            {"kind", "user_client"},
            {"password", "over-limit-pass"},
            {"mount_point_group_id", "mpg-basic"},
            {"concurrency_limit", 4},
        }, 5022).status == navcaster::storage::RepositoryStatus::Invalid, "domain access concurrency cannot exceed owner");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-dup-username"},
            {"owner_account_id", "acc-user"},
            {"username", "rover-user"},
            {"kind", "user_client"},
            {"password", "dup-pass"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5023).status == navcaster::storage::RepositoryStatus::Conflict, "domain access username unique");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-user-1"},
            {"owner_account_id", "acc-user"},
            {"username", "new-failed-access-name"},
            {"kind", "user_client"},
            {"password", "dup-id-pass"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5023).status == navcaster::storage::RepositoryStatus::Conflict, "domain duplicate access account id rejected");
        expect_true(domain_redis.hget(navcaster::redis_keys::AACC_USERNAME, "new-failed-access-name").is_null(), "domain failed access create does not tombstone username");

        domain_result = domain_repo.create_access_account({
            {"access_account_id", "aacc-supplier-1"},
            {"owner_account_id", "acc-supplier"},
            {"username", "station-supplier"},
            {"kind", "supplier_station"},
            {"password", "station-pass"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5024);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain supplier station create ok");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-admin-station"},
            {"owner_account_id", "acc-admin"},
            {"username", "admin-station"},
            {"kind", "supplier_station"},
            {"password", "admin-station-pass"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5025).status == navcaster::storage::RepositoryStatus::Ok, "domain admin super role creates own station access account");

        expect_true(domain_repo.delete_access_account("aacc-user-1", 5030).status == navcaster::storage::RepositoryStatus::Ok, "domain delete access tombstones");
        expect_true(domain_redis.hget(navcaster::redis_keys::AACC_ACTIVE, "rover-user").is_null(), "domain delete access removes active index");
        expect_eq(domain_redis.hget(navcaster::redis_keys::AACC_USERNAME, "rover-user").value("status", std::string{}), "deleted", "domain access username tombstone");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "aacc-user-2"},
            {"owner_account_id", "acc-user"},
            {"username", "rover-user"},
            {"kind", "user_client"},
            {"password", "reused-pass"},
            {"mount_point_group_id", "mpg-basic"},
        }, 5031).status == navcaster::storage::RepositoryStatus::Conflict, "domain deleted access username cannot be reused");

        expect_true(domain_repo.create_subscription({
            {"subscription_id", "sub-user-1"},
            {"account_id", "acc-user"},
            {"group_ids", nlohmann::json::array({"mpg-basic"})},
            {"status", "active"},
            {"expire_time", 7000},
        }, 5040).status == navcaster::storage::RepositoryStatus::Ok, "domain subscription create");
        expect_true(domain_redis.hget(navcaster::redis_keys::sub_account("acc-user").c_str(), "sub-user-1").is_object(), "domain subscription account index");
        domain_result = domain_repo.create_subscription({
            {"subscription_id", "sub-plan-user"},
            {"account_id", "acc-user"},
            {"plan_id", "plan-basic"},
            {"status", "active"},
        }, 5040);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain subscription create from plan");
        expect_eq(domain_result.record.value("plan_id", std::string{}), "plan-basic", "domain subscription stores plan id");
        expect_eq_int(domain_result.record.value("price_cents", 0), 12900, "domain subscription inherits plan price");
        expect_eq_int(domain_result.record.value("duration_days", 0), 45, "domain subscription inherits plan duration");
        expect_eq_int(domain_result.record.value("expire_time", 0), 5040 + 45 * 86400, "domain subscription plan computes expire time");
        expect_true(domain_result.record["plan_snapshot"].is_object(), "domain subscription stores plan snapshot");
        expect_true(domain_redis.hget(navcaster::redis_keys::sub_account("acc-user").c_str(), "sub-plan-user").is_object(), "domain subscription plan account index");
        expect_true(domain_repo.delete_subscription_plan("plan-basic", 5040).status == navcaster::storage::RepositoryStatus::Ok, "domain subscription plan delete");
        expect_true(domain_repo.create_subscription({
            {"subscription_id", "sub-deleted-plan"},
            {"account_id", "acc-user"},
            {"plan_id", "plan-basic"},
        }, 5040).status == navcaster::storage::RepositoryStatus::NotFound, "domain subscription rejects deleted plan");
        expect_true(domain_repo.update_subscription("sub-user-1", {
            {"group_ids", nlohmann::json::array({"mpg-basic"})},
            {"status", "disabled"},
            {"expire_time", 7100},
        }, 5041).status == navcaster::storage::RepositoryStatus::Ok, "domain subscription update");
        expect_eq(domain_redis.hget(navcaster::redis_keys::sub_account("acc-user").c_str(), "sub-user-1").value("status", std::string{}), "disabled", "domain subscription account index updates");
        expect_true(domain_repo.delete_subscription("sub-user-1", 5042).status == navcaster::storage::RepositoryStatus::Ok, "domain subscription delete");
        expect_true(domain_redis.hget(navcaster::redis_keys::sub_account("acc-user").c_str(), "sub-user-1").is_null(), "domain subscription delete removes account index");

        expect_true(domain_repo.create_redeem_code({
            {"code", "RC-DOMAIN"},
            {"amount_cents", 500},
            {"max_redemptions", 2},
        }, 5043).status == navcaster::storage::RepositoryStatus::Ok, "domain redeem code create");
        expect_true(domain_repo.redeem_code("RC-DOMAIN", "acc-user", {
            {"period", "202606"},
            {"operator_note", "domain redeem"},
        }, "202606", 5044).status == navcaster::storage::RepositoryStatus::Ok, "domain redeem code applies balance");
        expect_eq_int(domain_redis.hget(navcaster::redis_keys::ACC_RECORD, "acc-user").value("balance_cents", 0), 10500, "domain redeem updates balance");
        expect_true(domain_redis.hget(navcaster::redis_keys::redeem_account("acc-user").c_str(), "redeem:RC-DOMAIN:acc-user").is_object(), "domain redeem account index");
        expect_true(domain_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "ledger:redeem:RC-DOMAIN:acc-user").is_object(), "domain redeem writes ledger");
        expect_true(domain_repo.redeem_code("RC-DOMAIN", "acc-user", {}, "202606", 5045).status == navcaster::storage::RepositoryStatus::Conflict, "domain redeem rejects duplicate account redemption");

        domain_result = domain_repo.upsert_station_record({
            {"mountpoint", "BASE01"},
            {"station_id", "station-base01"},
            {"last_access_account_id", "aacc-supplier-1"},
            {"last_supplier_account_id", "acc-supplier"},
            {"current_online", true},
        }, 5050);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain station record upsert");
        expect_eq(domain_redis.hget(navcaster::redis_keys::STATION_RECORD, "BASE01").value("last_supplier_account_id", std::string{}), "acc-supplier", "domain station supplier snapshot");
        expect_true(domain_repo.append_station_event({
            {"event_id", "station-event-1"},
            {"mountpoint", "BASE01"},
            {"event_type", "login"},
            {"supplier_account_id", "acc-supplier"},
            {"access_account_id", "aacc-supplier-1"},
            {"session_id", "session-1"},
        }, 5051).status == navcaster::storage::RepositoryStatus::Ok, "domain station event append");
        auto station_events = domain_redis.lrange(navcaster::redis_keys::station_event("BASE01").c_str(), 0, 0);
        expect_eq(station_events[0].value("access_account_id", std::string{}), "aacc-supplier-1", "domain station event trace access account");
        domain_result = domain_repo.upsert_station_record({
            {"mountpoint", "BASE01"},
            {"last_access_account_id", "aacc-supplier-1"},
            {"current_online", false},
        }, 5052);
        expect_eq(domain_result.record.value("station_id", std::string{}), "station-base01", "domain station upsert preserves station id");
        expect_eq_int(domain_result.record.value("first_seen_time", 0), 5050, "domain station upsert preserves first seen");

        expect_true(domain_repo.append_balance_ledger({
            {"ledger_id", "ledger-1"},
            {"account_id", "acc-user"},
            {"delta_cents", -10},
            {"balance_after_cents", 9990},
            {"source", "billing_tick"},
        }, "202606", 5060).status == navcaster::storage::RepositoryStatus::Ok, "domain balance ledger append");
        expect_true(domain_repo.append_balance_ledger({
            {"ledger_id", "ledger-1"},
            {"account_id", "acc-user"},
        }, "202606", 5061).status == navcaster::storage::RepositoryStatus::Conflict, "domain balance ledger duplicate");

        nlohmann::json billing_entry = {
            {"billing_id", "bill-1"},
            {"fingerprint", "fp-1"},
            {"account_id", "acc-user"},
            {"access_account_id", "aacc-user-1"},
            {"mountpoint", "BASE01"},
            {"used_seconds", 60},
            {"stat_cost_cents", 2},
            {"actual_debit_cents", 2},
        };
        expect_true(domain_repo.append_billing_usage(billing_entry, "202606", 5070).status == navcaster::storage::RepositoryStatus::Ok, "domain billing append");
        expect_true(domain_repo.append_billing_usage(billing_entry, "202606", 5071).status == navcaster::storage::RepositoryStatus::Ok, "domain billing idempotent replay");
        billing_entry["fingerprint"] = "fp-2";
        expect_true(domain_repo.append_billing_usage(billing_entry, "202606", 5072).status == navcaster::storage::RepositoryStatus::Conflict, "domain billing fingerprint mismatch rejected");
        expect_true(domain_redis.hget(navcaster::redis_keys::bill_entry("202606").c_str(), "bill-1").is_object(), "domain billing entry key");
        auto bill_account_ids = domain_redis.lrange(navcaster::redis_keys::bill_account("acc-user", "202606").c_str(), 0, 0);
        expect_eq(bill_account_ids[0].get<std::string>(), "bill-1", "domain billing account index");

        expect_true(domain_repo.append_data_push_usage({
            {"usage_id", "push-usage-1"},
            {"account_id", "acc-user"},
            {"target_mountpoint", "BASE01"},
            {"used_seconds", 30},
        }, "202606", 5080).status == navcaster::storage::RepositoryStatus::Ok, "domain data push append");
        expect_true(domain_repo.append_data_push_usage_with_balance({
            {"usage_id", "push-usage-billed"},
            {"account_id", "acc-user"},
            {"target_mountpoint", "BASE01"},
            {"used_seconds", 30},
            {"stat_cost_cents", 5},
            {"actual_debit_cents", 5},
        }, "202606", 5080).status == navcaster::storage::RepositoryStatus::Ok, "domain data push append debits balance");
        expect_eq_int(domain_redis.hget(navcaster::redis_keys::ACC_RECORD, "acc-user").value("balance_cents", 0), 10495, "domain data push updates account balance");
        expect_true(domain_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "ledger:data_push:push-usage-billed").is_object(), "domain data push writes balance ledger");
        expect_true(domain_repo.append_data_push_usage_with_balance({
            {"usage_id", "push-usage-over-balance"},
            {"account_id", "acc-user"},
            {"target_mountpoint", "BASE01"},
            {"actual_debit_cents", 20000},
        }, "202606", 5080).status == navcaster::storage::RepositoryStatus::Conflict, "domain data push rejects insufficient balance");
        expect_true(domain_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "push-usage-over-balance").is_null(), "domain data push insufficient has no usage half write");
        expect_true(domain_repo.create_data_push_config({
            {"config_id", "domain-push-cfg"},
            {"name", "Domain Push"},
            {"target_mountpoint", "BASE01"},
            {"fixed_hourly_price_cents", 120},
        }, 5081).status == navcaster::storage::RepositoryStatus::Ok, "domain data push config create");
        domain_result = domain_repo.create_data_push_job({
            {"job_id", "domain-push-job"},
            {"account_id", "acc-user"},
            {"config_id", "domain-push-cfg"},
            {"used_seconds", 3600},
            {"period", "202606"},
        }, "202606", 5082);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain data push job create");
        expect_eq_int(domain_result.record.value("actual_debit_cents", 0), 120, "domain data push job cost");
        expect_eq_int(domain_redis.hget(navcaster::redis_keys::ACC_RECORD, "acc-user").value("balance_cents", 0), 10375, "domain data push job debits account");
        expect_true(domain_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "domain-push-job").is_object(), "domain data push job key");
        expect_true(domain_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "usage:data_push:domain-push-job").is_object(), "domain data push job writes usage");
        expect_true(domain_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "ledger:data_push:usage:data_push:domain-push-job").is_object(), "domain data push job writes ledger");
        expect_true(domain_repo.create_data_push_job({
            {"job_id", "domain-push-overdraw"},
            {"account_id", "acc-user"},
            {"config_id", "domain-push-cfg"},
            {"used_seconds", 400000},
            {"period", "202606"},
        }, "202606", 5083).status == navcaster::storage::RepositoryStatus::Conflict, "domain data push job rejects insufficient balance");
        expect_true(domain_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "domain-push-overdraw").is_null(), "domain data push job insufficient no job write");
        expect_true(domain_repo.append_supplier_supply_usage({
            {"usage_id", "supply-1"},
            {"supplier_account_id", "acc-supplier"},
            {"access_account_id", "aacc-supplier-1"},
            {"mountpoint", "BASE01"},
            {"used_seconds", 120},
            {"earning_cents", 5},
        }, "202606", 5081).status == navcaster::storage::RepositoryStatus::Ok, "domain supplier supply append");
        expect_true(domain_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "push-usage-1").is_object(), "domain data push key");
        expect_true(domain_redis.hget(navcaster::redis_keys::supply_usage("202606").c_str(), "supply-1").is_object(), "domain supply key");
        domain_result = domain_repo.create_supplier_settlement({
            {"supplier_account_id", "acc-supplier"},
            {"period", "202606"},
            {"operator_note", "domain settlement"},
        }, "202606", 5082);
        expect_true(domain_result.status == navcaster::storage::RepositoryStatus::Ok, "domain supplier settlement create");
        expect_eq(domain_result.record.value("supplier_account_id", std::string{}), "acc-supplier", "domain supplier settlement account");
        expect_eq_int(domain_result.record.value("usage_count", 0), 1, "domain supplier settlement usage count");
        expect_eq_int(domain_result.record.value("total_supply_seconds", 0), 120, "domain supplier settlement seconds");
        expect_eq_int(domain_result.record.value("total_earning_cents", 0), 5, "domain supplier settlement cents");
        expect_eq(domain_result.record.value("status", std::string{}), "pending_payment", "domain supplier settlement pending payment");
        expect_true(domain_redis.hget(navcaster::redis_keys::supply_earning("acc-supplier", "202606").c_str(), domain_result.id.c_str()).is_object(), "domain supplier settlement key");
        expect_eq(domain_redis.hget(navcaster::redis_keys::supply_usage("202606").c_str(), "supply-1").value("status", std::string{}), "settled", "domain supplier usage marked settled");
        expect_eq(domain_redis.hget(navcaster::redis_keys::supply_usage("202606").c_str(), "supply-1").value("settlement_id", std::string{}), domain_result.id, "domain supplier usage settlement id");
        auto payment_result = domain_repo.update_supplier_settlement_payment(domain_result.id, "acc-supplier", "202606", {
            {"status", "paid"},
            {"payment_method", "manual"},
            {"payment_ref", "PAY-DOMAIN"},
            {"payment_note", "domain paid"},
        }, 5084);
        expect_true(payment_result.status == navcaster::storage::RepositoryStatus::Ok, "domain supplier settlement mark paid");
        expect_eq(payment_result.record.value("status", std::string{}), "paid", "domain supplier settlement paid status");
        expect_eq(payment_result.record.value("payment_ref", std::string{}), "PAY-DOMAIN", "domain supplier settlement payment ref");
        expect_eq_int(payment_result.record.value("paid_time", 0), 5084, "domain supplier settlement paid time");
        expect_true(domain_repo.update_supplier_settlement_payment(domain_result.id, "acc-supplier", "202606", {{"status", "payment_failed"}}, 5085).status == navcaster::storage::RepositoryStatus::Conflict, "domain supplier settlement paid cannot fail");
        expect_true(domain_repo.create_supplier_settlement({{"supplier_account_id", "acc-supplier"}, {"period", "202606"}}, "202606", 5083).status == navcaster::storage::RepositoryStatus::Conflict, "domain supplier settlement no pending usage rejected");

        expect_true(domain_repo.delete_account("acc-user", 5090).status == navcaster::storage::RepositoryStatus::Ok, "domain account delete tombstones");
        expect_eq(domain_redis.hget(navcaster::redis_keys::ACC_USERNAME, "customer-a").value("status", std::string{}), "deleted", "domain account username tombstone");
        expect_true(domain_repo.create_account({
            {"account_id", "acc-user-reuse"},
            {"username", "customer-a"},
            {"role", "user"},
        }, 5091).status == navcaster::storage::RepositoryStatus::Conflict, "domain deleted account username cannot be reused");
        expect_true(domain_repo.update_account("acc-user", {
            {"role", "user"},
            {"balance_cents", 12000},
        }, 5092).status == navcaster::storage::RepositoryStatus::NotFound, "domain deleted account cannot be updated");
    }

    {
        FakeRedisHashClient operations_redis;
        navcaster::http_api::OperationsController operations(operations_redis, 6000);

        auto response = operations.session_subject("admin");
        expect_eq_int(response.status_code, 200, "operations session subject ok");
        auto response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("role", std::string{}), "admin", "operations session role");
        expect_true(response_body.value("compat_admin", false), "operations session compat admin");

        response = operations.create_account(R"({"account_id":"op-user","username":"op-customer","role":"user","balance_cents":5000,"concurrency_limit":2})");
        expect_eq_int(response.status_code, 201, "operations create account");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("account_id", std::string{}), "op-user", "operations create account id");
        expect_missing(response_body, "password_hash", "operations account response strips password hash");
        expect_true(operations.create_account(R"({"account_id":"op-bad","username":"op-bad","role":"operator"})").status_code == 400, "operations invalid account role");
        expect_true(operations.create_account(R"({"account_id":"op-other","username":"op-customer","role":"user"})").status_code == 409, "operations duplicate account username");

        response = operations.list_accounts();
        expect_eq_int(response.status_code, 200, "operations list accounts");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-user"), "operations list contains account");

        response = operations.update_account("op-user", R"({"role":"user","username":"op-customer","balance_cents":7000,"concurrency_limit":2})");
        expect_eq_int(response.status_code, 200, "operations update account");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("balance_cents", 0), 7000, "operations account update balance");
        response = operations.create_account(R"({"account_id":"op-supplier","username":"op-supplier","role":"supplier","concurrency_limit":2})");
        expect_eq_int(response.status_code, 201, "operations create supplier account");

        expect_eq_int(operations.create_mount_point_group(R"({"group_id":"op-group","name":"Operations Group"})").status_code, 201, "operations create group");
        expect_eq_int(operations.create_mount_point(R"({"mountpoint":"OPBASE","hourly_price_cents":120})").status_code, 201, "operations create mount point");
        expect_eq_int(operations.add_mount_point_group_member("op-group", R"({"mountpoint":"OPBASE"})").status_code, 201, "operations add group member");
        expect_eq_int(operations.grant_account_group("op-user", R"({"group_id":"op-group"})").status_code, 201, "operations grant account group");
        expect_eq_int(operations.grant_account_group("op-supplier", R"({"group_id":"op-group"})").status_code, 201, "operations grant supplier group");
        expect_true(operations_redis.hget(navcaster::redis_keys::acc_group("op-user").c_str(), "op-group").is_object(), "operations grant writes ACC:GROUP");

        navcaster::storage::AccountDomainRepository domain_repo(operations_redis);
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "op-aacc"},
            {"owner_account_id", "op-user"},
            {"username", "op-rover"},
            {"kind", "user_client"},
            {"password", "op-rover-pass"},
            {"mount_point_group_id", "op-group"},
        }, 6010).status == navcaster::storage::RepositoryStatus::Ok, "operations fixture access account");
        expect_true(domain_repo.create_access_account({
            {"access_account_id", "op-station"},
            {"owner_account_id", "op-supplier"},
            {"username", "op-station"},
            {"kind", "supplier_station"},
            {"password", "op-station-pass"},
            {"mount_point_group_id", "op-group"},
        }, 6011).status == navcaster::storage::RepositoryStatus::Ok, "operations fixture supplier access account");
        response = operations.list_access_accounts();
        expect_eq_int(response.status_code, 200, "operations list access accounts");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-aacc"), "operations access account read-only list");

        expect_eq_int(operations.create_subscription(R"({"subscription_id":"op-sub","account_id":"op-user","group_ids":["op-group"],"expire_time":9000})").status_code, 201, "operations create subscription");
        response = operations.create_subscription_plan(R"({"plan_id":"op-plan","name":"OP Plan","group_ids":["op-group"],"price_cents":19900,"duration_days":60})");
        expect_eq_int(response.status_code, 201, "operations create subscription plan");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("plan_id", std::string{}), "op-plan", "operations subscription plan id");
        expect_eq_int(operations.update_subscription_plan("op-plan", R"({"price_cents":24900})").status_code, 200, "operations update subscription plan");
        response = operations.list_subscription_plans();
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-plan"), "operations list subscription plans");
        response = operations.create_subscription(R"({"subscription_id":"op-plan-sub","account_id":"op-user","plan_id":"op-plan"})");
        expect_eq_int(response.status_code, 201, "operations create subscription from plan");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("plan_id", std::string{}), "op-plan", "operations subscription plan id stored");
        expect_eq_int(response_body.value("price_cents", 0), 24900, "operations subscription inherits updated plan price");
        expect_true(response_body["plan_snapshot"].is_object(), "operations subscription stores plan snapshot");
        expect_eq_int(operations.delete_subscription_plan("op-plan").status_code, 200, "operations delete subscription plan");
        expect_eq_int(operations.create_subscription(R"({"subscription_id":"op-deleted-plan-sub","account_id":"op-user","plan_id":"op-plan"})").status_code, 404, "operations subscription rejects deleted plan");
        expect_eq_int(operations.update_subscription("op-sub", R"({"group_ids":["op-group"],"status":"disabled","expire_time":9100})").status_code, 200, "operations update subscription");
        expect_eq(operations_redis.hget(navcaster::redis_keys::sub_account("op-user").c_str(), "op-sub").value("status", std::string{}), "disabled", "operations subscription account index sync");
        response = operations.list_subscriptions();
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-sub"), "operations list subscriptions");

        expect_eq_int(operations.append_balance_adjustment("op-user", R"({"ledger_id":"op-ledger","period":"202606","delta_cents":1000,"balance_after_cents":8000,"source":"manual_adjustment"})").status_code, 201, "operations balance adjustment");
        expect_true(operations_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "op-ledger").is_object(), "operations balance ledger key");
        expect_eq_int(operations_redis.hget(navcaster::redis_keys::ACC_RECORD, "op-user").value("balance_cents", 0), 8000, "operations balance adjustment updates account");
        expect_eq_int(operations.create_redeem_code(R"({"code":"RC-OP","amount_cents":250,"max_redemptions":1})").status_code, 201, "operations create redeem code");
        expect_eq_int(operations.redeem_code("RC-OP", "op-user", R"({"period":"202606","operator_note":"topup"})").status_code, 201, "operations redeem code");
        expect_eq_int(operations_redis.hget(navcaster::redis_keys::ACC_RECORD, "op-user").value("balance_cents", 0), 8250, "operations redeem updates account balance");
        expect_eq_int(operations.redeem_code("RC-OP", "op-user", R"({"period":"202606"})").status_code, 409, "operations redeem duplicate rejected");
        response = operations.list_redeem_codes();
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("RC-OP"), "operations list redeem codes");

        expect_eq_int(domain_repo.upsert_station_record({{"mountpoint", "OPBASE"}, {"station_id", "station-op"}}, 6020).status == navcaster::storage::RepositoryStatus::Ok ? 200 : 500, 200, "operations fixture station");
        response = operations.list_stations();
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("OPBASE"), "operations list stations");

        expect_true(domain_repo.append_billing_usage({
            {"billing_id", "op-bill"},
            {"fingerprint", "op-fp"},
            {"account_id", "op-user"},
            {"access_account_id", "op-aacc"},
            {"mountpoint", "OPBASE"},
        }, "202606", 6030).status == navcaster::storage::RepositoryStatus::Ok, "operations fixture billing usage");
        response = operations.list_usage("202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-bill"), "operations list billing usage");

        expect_true(domain_repo.append_data_push_usage_with_balance({
            {"usage_id", "op-data-push"},
            {"account_id", "op-user"},
            {"target_mountpoint", "OPBASE"},
            {"used_seconds", 10},
            {"stat_cost_cents", 3},
            {"actual_debit_cents", 3},
        }, "202606", 6035).status == navcaster::storage::RepositoryStatus::Ok, "operations fixture data push usage");
        response = operations.list_data_push_usage("202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-data-push"), "operations list data push usage");
        response = operations.create_data_push_config(R"({"config_id":"op-push-cfg","name":"OP Push","target_mountpoint":"OPBASE","fixed_hourly_price_cents":60})");
        expect_eq_int(response.status_code, 201, "operations create data push config");
        response = operations.get_data_push_config("op-push-cfg");
        expect_eq_int(response.status_code, 200, "operations get data push config");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("target_mountpoint", std::string{}), "OPBASE", "operations data push config target");
        response = operations.list_data_push_configs();
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-push-cfg"), "operations list data push configs");
        expect_true(domain_repo.create_data_push_job({
            {"job_id", "op-push-job"},
            {"account_id", "op-user"},
            {"config_id", "op-push-cfg"},
            {"used_seconds", 3600},
            {"period", "202606"},
        }, "202606", 6036).status == navcaster::storage::RepositoryStatus::Ok, "operations fixture data push job");
        response = operations.list_data_push_jobs("202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-push-job"), "operations list data push jobs");
        response = operations.delete_data_push_config("op-push-cfg");
        expect_eq_int(response.status_code, 200, "operations delete data push config");
        response = operations.get_data_push_config("op-push-cfg");
        expect_eq_int(response.status_code, 404, "operations get deleted data push config");

        expect_true(domain_repo.append_supplier_supply_usage({
            {"usage_id", "op-supply"},
            {"supplier_account_id", "op-supplier"},
            {"access_account_id", "op-station"},
            {"mountpoint", "OPBASE"},
            {"used_seconds", 90},
            {"earning_cents", 15},
        }, "202606", 6040).status == navcaster::storage::RepositoryStatus::Ok, "operations fixture supply usage");
        response = operations.list_supply_usage("202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("op-supply"), "operations list supply usage");
        response = operations.create_supplier_settlement(R"({"supplier_account_id":"op-supplier","period":"202606","operator_note":"op settlement"})");
        expect_eq_int(response.status_code, 201, "operations create supplier settlement");
        response_body = nlohmann::json::parse(response.body);
        const std::string op_settlement_id = response_body.value("settlement_id", std::string{});
        expect_eq_int(response_body.value("usage_count", 0), 1, "operations supplier settlement usage count");
        expect_eq_int(response_body.value("total_earning_cents", 0), 15, "operations supplier settlement cents");
        expect_eq(response_body.value("status", std::string{}), "pending_payment", "operations supplier settlement pending payment");
        expect_eq(operations_redis.hget(navcaster::redis_keys::supply_usage("202606").c_str(), "op-supply").value("status", std::string{}), "settled", "operations supplier usage settled");
        response = operations.list_supplier_settlements("202606", "op-supplier");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains(op_settlement_id), "operations list supplier settlements by supplier");
        response = operations.get_supplier_settlement(op_settlement_id, "202606", "op-supplier");
        expect_eq_int(response.status_code, 200, "operations get supplier settlement");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("settlement_id", std::string{}), op_settlement_id, "operations get supplier settlement id");
        response = operations.update_supplier_settlement_payment(op_settlement_id, "202606", "op-supplier", R"({"status":"paid","payment_method":"manual","payment_ref":"PAY-OP","payment_note":"paid by ops"})");
        expect_eq_int(response.status_code, 200, "operations supplier settlement mark paid");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "paid", "operations supplier settlement paid status");
        expect_eq(response_body.value("payment_ref", std::string{}), "PAY-OP", "operations supplier settlement payment ref");
        expect_eq_int(response_body.value("paid_time", 0), 6000, "operations supplier settlement paid time");
        expect_eq_int(operations.update_supplier_settlement_payment(op_settlement_id, "202606", "op-supplier", R"({"status":"payment_failed"})").status_code, 409, "operations paid settlement cannot fail");
        expect_eq_int(operations.create_supplier_settlement(R"({"supplier_account_id":"op-supplier","period":"202606"})").status_code, 409, "operations supplier settlement no pending usage rejected");

        response = operations.operations_monitor("202606");
        expect_eq_int(response.status_code, 200, "operations monitor basic snapshot");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("period", std::string{}), "202606", "operations monitor period");
        expect_true(response_body["accounts"].value("total_count", 0) >= 2, "operations monitor account count");
        expect_true(response_body["data_push"].value("usage_count", 0) >= 1, "operations monitor data push usage count");
        expect_eq_int(response_body["supply"]["settlements"].value("paid_count", 0), 1, "operations monitor paid settlement count");

        operations_redis.hset(navcaster::redis_keys::ACC_RECORD, "op-negative", nlohmann::json{
            {"account_id", "op-negative"},
            {"username", "op-negative"},
            {"role", "user"},
            {"status", "active"},
            {"balance_cents", -25},
            {"expire_time", 0},
        }.dump());
        operations_redis.hset(navcaster::redis_keys::ACC_RECORD, "op-low", nlohmann::json{
            {"account_id", "op-low"},
            {"username", "op-low"},
            {"role", "user"},
            {"status", "active"},
            {"balance_cents", 500},
            {"expire_time", 0},
        }.dump());
        operations_redis.hset(navcaster::redis_keys::data_push_job("202606").c_str(), "op-monitor-failed", nlohmann::json{
            {"job_id", "op-monitor-failed"},
            {"account_id", "op-user"},
            {"config_id", "op-push-cfg"},
            {"target_mountpoint", "OPBASE"},
            {"execution_mode", "relay_push"},
            {"relay_uid", "data_push:op-monitor-failed"},
            {"status", "failed"},
            {"failure_reason", "relay_runtime_missing"},
            {"failure_time", 6060},
            {"period", "202606"},
        }.dump());
        expect_true(domain_repo.append_supplier_supply_usage({
            {"usage_id", "op-supply-pending"},
            {"supplier_account_id", "op-supplier"},
            {"access_account_id", "op-station"},
            {"mountpoint", "OPBASE"},
            {"used_seconds", 120},
            {"earning_cents", 25},
        }, "202606", 6045).status == navcaster::storage::RepositoryStatus::Ok, "operations monitor fixture pending supply");
        operations_redis.hset(navcaster::redis_keys::supply_earning("op-supplier", "202606").c_str(), "op-pending-payment", nlohmann::json{
            {"settlement_id", "op-pending-payment"},
            {"supplier_account_id", "op-supplier"},
            {"period", "202606"},
            {"usage_count", 1},
            {"total_supply_seconds", 30},
            {"total_earning_cents", 33},
            {"status", "pending_payment"},
            {"create_time", 6061},
        }.dump());
        response = operations.operations_monitor("202606");
        expect_eq_int(response.status_code, 200, "operations monitor risk snapshot");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body["accounts"].value("negative_balance_count", 0), 1, "operations monitor negative balance count");
        expect_true(response_body["accounts"].value("low_balance_count", 0) >= 1, "operations monitor low balance count");
        expect_eq_int(response_body["data_push"].value("failed_count", 0), 1, "operations monitor data push failed count");
        expect_true(!response_body["data_push"]["recent_failed_jobs"].empty(), "operations monitor recent failed jobs");
        expect_eq_int(response_body["supply"].value("pending_usage_count", 0), 1, "operations monitor pending supply count");
        expect_true(response_body["supply"]["settlements"].value("pending_payment_count", 0) >= 1, "operations monitor pending settlement count");
        expect_true(!response_body["alerts"].empty(), "operations monitor alerts generated");
        expect_eq(response_body["alert_policy"].value("policy_id", std::string{}), "default", "operations monitor alert policy snapshot");
        expect_eq_int(response_body["accounts"].value("low_balance_threshold_cents", 0), 1000, "operations monitor default low balance threshold");
        auto has_monitor_alert = [](const nlohmann::json &alerts, const std::string &code) {
            if (!alerts.is_array())
            {
                return false;
            }
            for (const auto &alert : alerts)
            {
                if (alert.is_object() && alert.value("code", std::string{}) == code)
                {
                    return true;
                }
            }
            return false;
        };
        expect_true(has_monitor_alert(response_body["alerts"], "supplier_pending_payment"), "operations monitor pending payment alert");
        response = operations.operations_alert_policy();
        expect_eq_int(response.status_code, 200, "operations alert policy default");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("low_balance_threshold_cents", 0), 1000, "operations alert policy default low balance threshold");
        response = operations.update_operations_alert_policy(R"({"low_balance_threshold_cents":7000,"supplier_pending_payment_enabled":false,"supplier_usage_pending_enabled":false,"data_push_failed_threshold":2})");
        expect_eq_int(response.status_code, 200, "operations alert policy update");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("low_balance_threshold_cents", 0), 7000, "operations alert policy updated low balance threshold");
        expect_true(!response_body.value("supplier_pending_payment_enabled", true), "operations alert policy disables pending payment");
        expect_true(operations_redis.hget(navcaster::redis_keys::OPS_ALERT_POLICY, "default").is_object(), "operations alert policy persisted");
        response = operations.operations_monitor("202606");
        expect_eq_int(response.status_code, 200, "operations monitor policy snapshot");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body["accounts"].value("low_balance_threshold_cents", 0), 7000, "operations monitor uses alert policy threshold");
        expect_true(has_monitor_alert(response_body["alerts"], "low_balance"), "operations monitor low balance alert after policy update");
        expect_true(!has_monitor_alert(response_body["alerts"], "supplier_pending_payment"), "operations monitor disabled pending payment alert");
        expect_true(!has_monitor_alert(response_body["alerts"], "supplier_usage_pending"), "operations monitor disabled pending supply alert");
        expect_true(!has_monitor_alert(response_body["alerts"], "data_push_failed"), "operations monitor data push threshold suppresses alert");
        response = operations.update_operations_alert_policy(R"({"low_balance_threshold_cents":-1})");
        expect_eq_int(response.status_code, 400, "operations alert policy rejects invalid low balance threshold");

        FakeRedisHashClient monitor_empty_redis;
        navcaster::http_api::OperationsController monitor_empty(monitor_empty_redis, 6100);
        response = monitor_empty.operations_monitor("202606");
        expect_eq_int(response.status_code, 200, "operations monitor empty snapshot");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body["accounts"].value("total_count", -1), 0, "operations monitor empty account count");
        expect_true(response_body["data_push"]["maintenance"].value("enabled", false), "operations monitor empty default maintenance");

        expect_eq_int(operations.delete_account("op-user").status_code, 200, "operations delete account");
        expect_true(operations.create_account(R"({"account_id":"op-reuse","username":"op-customer","role":"user"})").status_code == 409, "operations deleted username cannot be reused");
    }

    {
        FakeRedisHashClient self_redis;
        navcaster::storage::AccountDomainRepository domain_repo(self_redis);
        navcaster::http_api::SelfServiceController self_service(self_redis, 7000);

        expect_true(domain_repo.create_account({
            {"account_id", "self-user"},
            {"username", "self-user-login"},
            {"role", "user"},
            {"password", "user-pass"},
            {"balance_cents", 100},
            {"concurrency_limit", 2},
        }, 7001).status == navcaster::storage::RepositoryStatus::Ok, "self service user account fixture");
        expect_true(domain_repo.create_account({
            {"account_id", "self-supplier"},
            {"username", "self-supplier-login"},
            {"role", "supplier"},
            {"password", "supplier-pass"},
            {"concurrency_limit", 2},
        }, 7002).status == navcaster::storage::RepositoryStatus::Ok, "self service supplier account fixture");
        expect_true(domain_repo.create_account({
            {"account_id", "self-admin"},
            {"username", "self-admin-login"},
            {"role", "admin"},
            {"password", "admin-pass"},
            {"concurrency_limit", 3},
        }, 7003).status == navcaster::storage::RepositoryStatus::Ok, "self service admin account fixture");
        expect_missing(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-user"), "password", "domain account password stripped");
        expect_has(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-user"), "password_hash", "domain account password hashed");

        expect_true(domain_repo.create_mount_point_group({{"group_id", "self-group"}, {"name", "Self Group"}}, 7010).status == navcaster::storage::RepositoryStatus::Ok, "self service group fixture");
        expect_true(domain_repo.create_mount_point_group({{"group_id", "other-group"}, {"name", "Other Group"}}, 7011).status == navcaster::storage::RepositoryStatus::Ok, "self service other group fixture");
        expect_true(domain_repo.create_mount_point({{"mountpoint", "SELFBASE"}, {"hourly_price_cents", 100}}, 7012).status == navcaster::storage::RepositoryStatus::Ok, "self service mount fixture");
        expect_true(domain_repo.add_mount_point_group_member("self-group", {{"mountpoint", "SELFBASE"}}, 7013).status == navcaster::storage::RepositoryStatus::Ok, "self service group member fixture");
        expect_true(domain_repo.grant_account_group("self-user", {{"group_id", "self-group"}}, 7014).status == navcaster::storage::RepositoryStatus::Ok, "self service user group grant");
        expect_true(domain_repo.grant_account_group("self-supplier", {{"group_id", "self-group"}}, 7015).status == navcaster::storage::RepositoryStatus::Ok, "self service supplier group grant");
        expect_true(domain_repo.grant_account_group("self-admin", {{"group_id", "self-group"}}, 7016).status == navcaster::storage::RepositoryStatus::Ok, "self service admin group grant");

        int self_token_id = 0;
        navcaster::http_api::AuthSessionService sessions([&]() {
            return std::string("self-token-") + std::to_string(++self_token_id);
        });
        auto login_response = sessions.login(
            R"({"username":"self-user-login","password":"user-pass"})",
            {"admin", "admin"},
            nlohmann::json::object(),
            &self_redis);
        expect_eq_int(login_response.status_code, 200, "self service user login ok");
        auto login_body = nlohmann::json::parse(login_response.body);
        expect_eq(login_body.value("role", std::string{}), "user", "self service user login role");
        expect_eq(login_body.value("account_id", std::string{}), "self-user", "self service user login account");
        expect_missing(login_body["account"], "password_hash", "self service login strips account hash");
        const auto user_subject = sessions.lookup_subject(login_body.value("token", std::string{}));
        expect_eq(user_subject.role, "user", "self service lookup subject role");
        expect_eq(sessions.lookup_user(login_body.value("token", std::string{})), "self-user-login", "self service lookup user");

        login_response = sessions.login(
            R"({"username":"self-supplier-login","password":"supplier-pass"})",
            {"admin", "admin"},
            nlohmann::json::object(),
            &self_redis);
        expect_eq_int(login_response.status_code, 200, "self service supplier login ok");
        const auto supplier_subject = sessions.lookup_subject(nlohmann::json::parse(login_response.body).value("token", std::string{}));

        login_response = sessions.login(
            R"({"username":"admin","password":"admin"})",
            {"admin", "admin"},
            nlohmann::json::object(),
            &self_redis);
        expect_eq_int(login_response.status_code, 200, "self service compat admin login ok");
        auto compat_subject = sessions.lookup_subject(nlohmann::json::parse(login_response.body).value("token", std::string{}));
        expect_true(compat_subject.compat_admin, "self service compat admin subject");
        expect_eq(compat_subject.role, "admin", "self service compat admin role");

        auto response = self_service.profile(user_subject, "me");
        expect_eq_int(response.status_code, 200, "self service user profile");
        auto response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("account_id", std::string{}), "self-user", "self service profile account id");
        expect_missing(response_body, "password_hash", "self service profile strips hash");

        response = self_service.allowed_groups(user_subject);
        expect_eq_int(response.status_code, 200, "self service allowed groups");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-group"), "self service allowed groups contains grant");
        response = self_service.mount_points(user_subject);
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("SELFBASE"), "self service mount points visible");

        response = self_service.create_access_account(user_subject, "me", R"({"access_account_id":"self-aacc","owner_account_id":"self-supplier","username":"self-rover","kind":"supplier_station","password":"rover-pass","mount_point_group_id":"self-group","concurrency_limit":1})");
        expect_eq_int(response.status_code, 201, "self service user creates own access account");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("owner_account_id", std::string{}), "self-user", "self service overrides owner");
        expect_eq(response_body.value("kind", std::string{}), "user_client", "self service overrides kind");
        expect_missing(response_body, "password_hash", "self service create strips access hash");
        auto stored_access = self_redis.hget(navcaster::redis_keys::AACC_RECORD, "self-aacc");
        expect_eq(stored_access.value("owner_account_id", std::string{}), "self-user", "self service stored owner");
        expect_eq(stored_access.value("kind", std::string{}), "user_client", "self service stored kind");
        expect_has(stored_access, "password_hash", "self service stored password hash");
        expect_missing(stored_access, "password", "self service stored no plaintext password");

        response = self_service.create_access_account(user_subject, "me", R"({"access_account_id":"self-bad-group","username":"bad-group","mount_point_group_id":"other-group"})");
        expect_eq_int(response.status_code, 400, "self service rejects ungranted group");
        response = self_service.create_access_account(user_subject, "supplier", R"({"access_account_id":"self-bad-scope","username":"bad-scope","mount_point_group_id":"self-group"})");
        expect_eq_int(response.status_code, 403, "self service user rejected supplier scope");

        response = self_service.create_access_account(supplier_subject, "supplier", R"({"access_account_id":"self-station","username":"self-station","kind":"user_client","password":"station-pass","mount_point_group_id":"self-group"})");
        expect_eq_int(response.status_code, 201, "self service supplier creates station access account");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("kind", std::string{}), "supplier_station", "self service supplier kind forced");
        response = self_service.get_access_account(user_subject, "me", "self-station");
        expect_eq_int(response.status_code, 404, "self service user cannot read supplier access account");

        response = self_service.update_access_account(user_subject, "me", "self-aacc", R"({"owner_account_id":"self-supplier","kind":"supplier_station","mount_point_group_id":"self-group","status":"disabled","concurrency_limit":1})");
        expect_eq_int(response.status_code, 200, "self service user updates own access account");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "disabled", "self service update status");
        expect_eq(response_body.value("owner_account_id", std::string{}), "self-user", "self service update preserves owner");
        expect_eq(response_body.value("kind", std::string{}), "user_client", "self service update preserves kind");
        expect_true(self_redis.hget(navcaster::redis_keys::AACC_ACTIVE, "self-rover").is_null(), "self service disabled removes active index");
        response = self_service.update_access_account_password(user_subject, "me", "self-aacc", R"({"password":"rotated","password_salt":"salt","password_iterations":2})");
        expect_eq_int(response.status_code, 200, "self service user rotates access password");
        auto rotated_access = self_redis.hget(navcaster::redis_keys::AACC_RECORD, "self-aacc");
        expect_eq(rotated_access.value("password_hash", std::string{}), navcaster::account_schema::make_password_hash("rotated", "salt", 2), "self service access password hash");

        expect_true(domain_repo.append_billing_usage({
            {"billing_id", "self-bill"},
            {"fingerprint", "self-bill-fp"},
            {"account_id", "self-user"},
            {"access_account_id", "self-aacc"},
            {"mountpoint", "SELFBASE"},
        }, "202606", 7020).status == navcaster::storage::RepositoryStatus::Ok, "self service billing fixture");
        response = self_service.usage(user_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-bill"), "self service user usage filtered");
        expect_true(domain_repo.create_subscription({
            {"subscription_id", "self-sub"},
            {"account_id", "self-user"},
            {"group_ids", nlohmann::json::array({"self-group"})},
        }, 7021).status == navcaster::storage::RepositoryStatus::Ok, "self service subscription fixture");
        response = self_service.subscriptions(user_subject);
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-sub"), "self service user subscriptions filtered");
        expect_true(domain_repo.create_subscription_plan({
            {"plan_id", "self-plan"},
            {"name", "Self Plan"},
            {"group_ids", nlohmann::json::array({"self-group"})},
            {"price_cents", 30},
            {"duration_days", 7},
        }, 7022).status == navcaster::storage::RepositoryStatus::Ok, "self service subscription plan fixture");
        expect_true(domain_repo.create_subscription_plan({
            {"plan_id", "self-over-balance-plan"},
            {"name", "Self Over Balance Plan"},
            {"group_ids", nlohmann::json::array({"self-group"})},
            {"price_cents", 1000},
            {"duration_days", 30},
        }, 7022).status == navcaster::storage::RepositoryStatus::Ok, "self service over balance subscription plan fixture");
        expect_true(domain_repo.create_subscription_plan({
            {"plan_id", "self-disabled-plan"},
            {"name", "Self Disabled Plan"},
            {"group_ids", nlohmann::json::array({"self-group"})},
            {"price_cents", 5},
            {"duration_days", 1},
            {"status", "disabled"},
        }, 7022).status == navcaster::storage::RepositoryStatus::Ok, "self service disabled subscription plan fixture");
        response = self_service.subscription_plans(user_subject);
        expect_eq_int(response.status_code, 200, "self service subscription plans list");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-plan"), "self service subscription plans contains active");
        expect_true(!response_body.contains("self-disabled-plan"), "self service subscription plans hide disabled");
        response = self_service.purchase_subscription_plan(user_subject, "self-plan", R"({"period":"202606","subscription_id":"self-plan-purchase","ledger_id":"self-plan-ledger","operator_note":"buy"})");
        expect_eq_int(response.status_code, 201, "self service purchase subscription plan");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("account_id", std::string{}), "self-user", "self service purchase owner from subject");
        expect_eq(response_body.value("plan_id", std::string{}), "self-plan", "self service purchase plan id");
        expect_eq(response_body.value("ledger_id", std::string{}), "self-plan-ledger", "self service purchase ledger id");
        expect_eq_int(response_body.value("balance_after_cents", 0), 70, "self service purchase balance after");
        expect_eq_int(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-user").value("balance_cents", 0), 70, "self service purchase debits account");
        expect_true(self_redis.hget(navcaster::redis_keys::SUB_RECORD, "self-plan-purchase").is_object(), "self service purchase writes subscription");
        expect_true(self_redis.hget(navcaster::redis_keys::sub_account("self-user").c_str(), "self-plan-purchase").is_object(), "self service purchase writes account subscription index");
        expect_true(self_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "self-plan-ledger").is_object(), "self service purchase writes balance ledger");
        response = self_service.purchase_subscription_plan(user_subject, "self-over-balance-plan", R"({"period":"202606","subscription_id":"self-over-balance-sub","ledger_id":"self-over-balance-ledger"})");
        expect_eq_int(response.status_code, 409, "self service purchase rejects insufficient balance");
        expect_true(self_redis.hget(navcaster::redis_keys::SUB_RECORD, "self-over-balance-sub").is_null(), "self service failed purchase has no subscription");
        expect_true(self_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "self-over-balance-ledger").is_null(), "self service failed purchase has no ledger");
        expect_true(domain_repo.create_redeem_code({{"code", "RC-SELF"}, {"amount_cents", 50}}, 7022).status == navcaster::storage::RepositoryStatus::Ok, "self service redeem fixture");
        response = self_service.redeem_code(user_subject, "RC-SELF", R"({"period":"202606","operator_note":"self topup","account_id":"self-supplier","amount_cents":999})");
        expect_eq_int(response.status_code, 201, "self service redeem code");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("account_id", std::string{}), "self-user", "self service redeem owner from subject");
        expect_eq_int(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-user").value("balance_cents", 0), 120, "self service redeem updates balance");
        expect_eq_int(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-supplier").value("balance_cents", 0), 0, "self service redeem ignores body account");
        response = self_service.redeem_redemptions(user_subject);
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("redeem:RC-SELF:self-user"), "self service user redeem redemptions filtered");

        response = self_service.append_data_push_usage(user_subject, R"({"usage_id":"self-data-push","account_id":"self-supplier","target_mountpoint":"SELFBASE","used_seconds":20,"stat_cost_cents":7,"actual_debit_cents":7,"period":"202606","ledger_id":"client-ledger","balance_after_cents":999999})");
        expect_eq_int(response.status_code, 201, "self service data push append");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("account_id", std::string{}), "self-user", "self service data push overrides account id");
        expect_eq(response_body.value("ledger_id", std::string{}), "ledger:data_push:self-data-push", "self service data push overrides ledger id");
        expect_eq_int(response_body.value("balance_after_cents", 0), 113, "self service data push response balance");
        expect_eq_int(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-user").value("balance_cents", 0), 113, "self service data push debits account");
        expect_true(self_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "self-data-push").is_object(), "self service data push writes usage");
        expect_true(self_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "ledger:data_push:self-data-push").is_object(), "self service data push writes ledger");
        expect_true(self_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "client-ledger").is_null(), "self service data push ignores client ledger id");
        response = self_service.data_push_usage(user_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-data-push"), "self service data push filtered");
        expect_true(domain_repo.create_data_push_config({
            {"config_id", "self-push-cfg"},
            {"name", "Self Push"},
            {"target_mountpoint", "SELFBASE"},
            {"fixed_hourly_price_cents", 30},
        }, 7024).status == navcaster::storage::RepositoryStatus::Ok, "self service data push config fixture");
        expect_true(domain_repo.create_data_push_config({
            {"config_id", "self-disabled-push-cfg"},
            {"name", "Self Disabled Push"},
            {"target_mountpoint", "SELFBASE"},
            {"fixed_hourly_price_cents", 30},
            {"status", "disabled"},
        }, 7025).status == navcaster::storage::RepositoryStatus::Ok, "self service disabled data push config fixture");
        response = self_service.data_push_configs(user_subject);
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-push-cfg"), "self service data push configs active");
        expect_true(!response_body.contains("self-disabled-push-cfg"), "self service data push configs hide disabled");
        response = self_service.create_data_push_job(user_subject, R"({"job_id":"self-push-job","account_id":"self-supplier","config_id":"self-push-cfg","used_seconds":3600,"period":"202606","usage_id":"client-usage","ledger_id":"client-ledger"})");
        expect_eq_int(response.status_code, 201, "self service create data push job");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("account_id", std::string{}), "self-user", "self service data push job overrides account id");
        expect_eq(response_body.value("usage_id", std::string{}), "usage:data_push:self-push-job", "self service data push job owns usage id");
        expect_eq_int(response_body.value("actual_debit_cents", 0), 30, "self service data push job cost");
        expect_eq_int(self_redis.hget(navcaster::redis_keys::ACC_RECORD, "self-user").value("balance_cents", 0), 83, "self service data push job debits account");
        expect_eq(response_body.value("status", std::string{}), "completed", "self service ledger data push job completed");
        expect_eq(response_body.value("execution_mode", std::string{}), "ledger_only", "self service ledger data push job mode");
        response = self_service.data_push_jobs(user_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-push-job"), "self service data push jobs filtered");
        expect_true(self_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "usage:data_push:self-push-job").is_object(), "self service data push job writes usage");
        expect_true(self_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "ledger:data_push:usage:data_push:self-push-job").is_object(), "self service data push job writes ledger");
        auto invalid_relay_config = domain_repo.create_data_push_config({
            {"config_id", "self-invalid-relay-push-cfg"},
            {"name", "Invalid Relay Push"},
            {"target_mountpoint", "REMOTEBASE"},
            {"fixed_hourly_price_cents", 10},
            {"execution_mode", "relay_push"},
            {"source_mountpoint", "SELFBASE"},
        }, 7026);
        expect_true(invalid_relay_config.status == navcaster::storage::RepositoryStatus::Invalid, "data push relay config requires target host");
        expect_true(domain_repo.create_data_push_config({
            {"config_id", "self-relay-push-cfg"},
            {"name", "Self Relay Push"},
            {"target_mountpoint", "REMOTEBASE"},
            {"fixed_hourly_price_cents", 60},
            {"execution_mode", "relay_push"},
            {"source_mountpoint", "SELFBASE"},
            {"relay_target_host", "caster.example.test"},
            {"relay_target_port", 2101},
            {"relay_target_mountpoint", "REMOTEBASE"},
            {"relay_target_account", "target-user"},
            {"relay_target_password", "target-secret"},
            {"relay_push_type", 1},
        }, 7027).status == navcaster::storage::RepositoryStatus::Ok, "self service relay data push config fixture");
        response = self_service.create_data_push_job(user_subject, R"({"job_id":"self-relay-push-job","config_id":"self-relay-push-cfg","used_seconds":1800,"period":"202606"})");
        expect_eq_int(response.status_code, 201, "self service create relay data push job");
        response_body = nlohmann::json::parse(response.body);
        const std::string relay_uid = response_body.value("relay_uid", std::string{});
        expect_eq(response_body.value("status", std::string{}), "queued", "self service relay data push job queued");
        expect_eq(response_body.value("execution_mode", std::string{}), "relay_push", "self service relay data push job mode");
        expect_eq(relay_uid, "data_push:self-relay-push-job", "self service relay data push job uid");
        expect_missing(response_body["config_snapshot"], "relay_target_password", "self service relay job config snapshot strips password");
        expect_missing(response_body["relay_push_record"], "target_password", "self service relay job record strips password");
        auto relay_record = self_redis.hget(navcaster::redis_keys::PUSH_RECORD, relay_uid.c_str());
        expect_eq(relay_record.value("login_mpt", std::string{}), "SELFBASE", "data push relay record source mountpoint");
        expect_eq(relay_record.value("target_ip", std::string{}), "caster.example.test", "data push relay record target host");
        expect_eq_int(relay_record.value("target_port", 0), 2101, "data push relay record target port");
        expect_eq(relay_record.value("target_mpt", std::string{}), "REMOTEBASE", "data push relay record target mountpoint");
        expect_eq(relay_record.value("target_account", std::string{}), "target-user", "data push relay record target account");
        expect_eq(relay_record.value("target_password", std::string{}), "target-secret", "data push relay record stores target password");
        expect_true(relay_record.value("enabled", false), "data push relay record enabled");
        auto stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_missing(stored_relay_job["config_snapshot"], "relay_target_password", "stored relay job config snapshot strips password");
        expect_missing(stored_relay_job["relay_push_record"], "target_password", "stored relay job record snapshot strips password");
        expect_true(self_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "usage:data_push:self-relay-push-job").is_object(), "relay data push job writes usage");
        expect_true(self_redis.hget(navcaster::redis_keys::acc_balance_ledger("202606").c_str(), "ledger:data_push:usage:data_push:self-relay-push-job").is_object(), "relay data push job writes ledger");
        self_redis.hset(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str(), nlohmann::json{{"uid", relay_uid}, {"state", 1}, {"connect_key", "relay-connect"}, {"node_uid", "node-relay"}, {"node_name", "Relay Node"}}.dump());
        response = self_service.data_push_jobs(user_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body["self-relay-push-job"].value("status", std::string{}), "running", "self service relay data push job runtime status");
        expect_eq(response_body["self-relay-push-job"].value("relay_status", std::string{}), "running", "self service relay data push job relay status");
        expect_missing(response_body["self-relay-push-job"]["config_snapshot"], "relay_target_password", "self service relay list strips config password");
        expect_missing(response_body["self-relay-push-job"]["relay_push_record"], "target_password", "self service relay list strips record password");
        navcaster::http_api::OperationsController ops_self(self_redis, 7030);
        response = ops_self.list_data_push_jobs("202606");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body["self-relay-push-job"].value("status", std::string{}), "running", "operations relay data push job runtime status");
        expect_eq(response_body["self-relay-push-job"].value("relay_status", std::string{}), "running", "operations relay data push job relay status");
        expect_missing(response_body["self-relay-push-job"]["config_snapshot"], "relay_target_password", "operations relay list strips config password");
        expect_missing(response_body["self-relay-push-job"]["relay_push_record"], "target_password", "operations relay list strips record password");
        response = self_service.reconcile_data_push_job_runtime(user_subject, "self-push-job", "202606", R"({"period":"202606"})");
        expect_eq_int(response.status_code, 400, "self service rejects ledger data push reconcile");
        response = self_service.reconcile_data_push_job_runtime(supplier_subject, "self-relay-push-job", "202606", R"({"period":"202606"})");
        expect_eq_int(response.status_code, 403, "self service supplier cannot reconcile me data push job");
        response = self_service.reconcile_data_push_job_runtime(user_subject, "self-relay-push-job", "202606", R"({"period":"202606","operator_note":"user reconcile"})");
        expect_eq_int(response.status_code, 200, "self service reconciles relay data push runtime");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "running", "self service reconcile running status");
        expect_eq(response_body.value("relay_status", std::string{}), "running", "self service reconcile relay status");
        expect_eq(response_body.value("relay_connect_key", std::string{}), "relay-connect", "self service reconcile connect key");
        expect_eq(response_body.value("relay_node_uid", std::string{}), "node-relay", "self service reconcile node uid");
        expect_eq_int(response_body.value("runtime_reconcile_time", 0), 7000, "self service reconcile timestamp");
        stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_eq(stored_relay_job.value("status", std::string{}), "running", "stored reconcile running status");
        expect_eq(stored_relay_job["relay_state_snapshot"].value("connect_key", std::string{}), "relay-connect", "stored reconcile state snapshot");
        response = self_service.update_data_push_job_control(user_subject, "self-push-job", "202606", R"({"action":"cancel","period":"202606"})");
        expect_eq_int(response.status_code, 400, "self service rejects ledger data push control");
        response = self_service.update_data_push_job_control(user_subject, "missing-push-job", "202606", R"({"action":"cancel","period":"202606"})");
        expect_eq_int(response.status_code, 404, "self service rejects other data push job control");
        response = self_service.update_data_push_job_control(supplier_subject, "self-relay-push-job", "202606", R"({"action":"cancel","period":"202606"})");
        expect_eq_int(response.status_code, 403, "self service supplier cannot control me data push job");
        response = self_service.update_data_push_job_control(user_subject, "self-relay-push-job", "202606", R"({"action":"mark_failed","period":"202606"})");
        expect_eq_int(response.status_code, 400, "self service rejects admin-only data push control");
        response = self_service.update_data_push_job_control(user_subject, "self-relay-push-job", "202606", R"({"action":"cancel","period":"202606","operator_note":"user cancel"})");
        expect_eq_int(response.status_code, 200, "self service cancels relay data push job");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "cancelled", "self service cancel status");
        relay_record = self_redis.hget(navcaster::redis_keys::PUSH_RECORD, relay_uid.c_str());
        expect_true(!relay_record.value("enabled", true), "self service cancel disables push record");
        expect_true(self_redis.hget(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str()).is_null(), "self service cancel clears push stat");
        response = self_service.update_data_push_job_control(user_subject, "self-relay-push-job", "202606", R"({"action":"retry","period":"202606","operator_note":"user retry"})");
        expect_eq_int(response.status_code, 200, "self service retries relay data push job");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "queued", "self service retry status");
        relay_record = self_redis.hget(navcaster::redis_keys::PUSH_RECORD, relay_uid.c_str());
        expect_true(relay_record.value("enabled", false), "self service retry enables push record");
        response = ops_self.update_data_push_job_control("self-relay-push-job", "202606", R"({"action":"mark_failed","period":"202606","operator_note":"admin failed"})");
        expect_eq_int(response.status_code, 200, "operations marks relay data push failed");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "failed", "operations failed status");
        relay_record = self_redis.hget(navcaster::redis_keys::PUSH_RECORD, relay_uid.c_str());
        expect_true(!relay_record.value("enabled", true), "operations mark failed disables push record");
        response = ops_self.update_data_push_job_control("self-relay-push-job", "202606", R"({"action":"retry","period":"202606","operator_note":"admin retry"})");
        expect_eq_int(response.status_code, 200, "operations retries failed relay data push");
        response = ops_self.update_data_push_job_control("self-relay-push-job", "202606", R"({"action":"mark_completed","period":"202606","operator_note":"admin complete"})");
        expect_eq_int(response.status_code, 200, "operations marks relay data push completed");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "completed", "operations completed status");
        self_redis.hset(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str(), nlohmann::json{{"uid", relay_uid}, {"state", 1}, {"connect_key", "relay-after-complete"}, {"node_uid", "node-after"}}.dump());
        response = ops_self.reconcile_data_push_job_runtime("self-relay-push-job", "202606", R"({"period":"202606","operator_note":"admin reconcile complete"})");
        expect_eq_int(response.status_code, 200, "operations reconciles completed relay data push");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "completed", "operations reconcile does not reopen completed job");
        expect_eq(response_body.value("relay_status", std::string{}), "running", "operations reconcile records terminal runtime status");
        response = ops_self.reconcile_data_push_jobs_runtime("202606");
        expect_eq_int(response.status_code, 200, "operations batch reconciles relay data push jobs");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("updated_count", 0), 1, "operations batch reconcile updated count");
        expect_true(response_body["items"].contains("self-relay-push-job"), "operations batch reconcile includes relay job");
        self_redis.hdel(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str());
        response = ops_self.reconcile_data_push_job_runtime("self-relay-push-job", "202606", R"({"period":"202606","operator_note":"admin reconcile stopped"})");
        expect_eq_int(response.status_code, 200, "operations reconciles stopped relay data push");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "completed", "operations stopped reconcile keeps terminal status");
        expect_eq(response_body.value("relay_status", std::string{}), "stopped", "operations stopped reconcile status");
        self_redis.hset(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str(), nlohmann::json{{"uid", relay_uid}, {"state", 1}, {"connect_key", "relay-auto-terminal"}, {"node_uid", "node-auto-terminal"}}.dump());
        navcaster::http_api::OperationsController ops_maintenance_terminal(self_redis, 7100);
        response = ops_maintenance_terminal.maintain_data_push_jobs_runtime("202606", 300);
        expect_eq_int(response.status_code, 200, "operations auto maintains terminal relay data push");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("updated_count", 0), 1, "operations auto terminal updated count");
        expect_eq_int(response_body.value("failed_count", -1), 0, "operations auto terminal failed count");
        stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_eq(stored_relay_job.value("status", std::string{}), "completed", "operations auto maintenance does not reopen completed job");
        expect_eq(stored_relay_job.value("runtime_reconcile_action", std::string{}), "auto_reconcile", "operations auto maintenance action");
        expect_eq(stored_relay_job.value("relay_connect_key", std::string{}), "relay-auto-terminal", "operations auto maintenance terminal snapshot");
        navcaster::http_api::OperationsController ops_maintenance_config(self_redis, 7105);
        response = ops_maintenance_config.data_push_maintenance_config();
        expect_eq_int(response.status_code, 200, "operations data push maintenance default config");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.value("enabled", false), "operations data push maintenance default enabled");
        expect_eq_int(response_body.value("interval_seconds", 0), 60, "operations data push maintenance default interval");
        expect_eq_int(response_body.value("unhealthy_after_seconds", 0), 300, "operations data push maintenance default threshold");
        response = ops_maintenance_config.update_data_push_maintenance_config(R"({"enabled":false,"interval_seconds":120,"unhealthy_after_seconds":45})");
        expect_eq_int(response.status_code, 200, "operations data push maintenance update config");
        response_body = nlohmann::json::parse(response.body);
        expect_true(!response_body.value("enabled", true), "operations data push maintenance disabled");
        expect_eq_int(response_body.value("interval_seconds", 0), 120, "operations data push maintenance interval updated");
        expect_eq_int(response_body.value("unhealthy_after_seconds", 0), 45, "operations data push maintenance threshold updated");
        expect_true(self_redis.hget(navcaster::redis_keys::DATA_PUSH_MAINTENANCE, "default").is_object(), "operations data push maintenance persisted");
        response = ops_maintenance_config.update_data_push_maintenance_config(R"({"interval_seconds":1})");
        expect_eq_int(response.status_code, 400, "operations data push maintenance rejects low interval");
        navcaster::http_api::OperationsController ops_maintenance_disabled(self_redis, 7110);
        response = ops_maintenance_disabled.scheduled_data_push_maintenance("202606", 0);
        expect_eq_int(response.status_code, 200, "operations scheduled data push maintenance disabled ok");
        response_body = nlohmann::json::parse(response.body);
        expect_true(!response_body.value("executed", true), "operations scheduled data push maintenance not executed when disabled");
        expect_eq(response_body.value("skip_reason", std::string{}), "disabled", "operations scheduled data push maintenance disabled reason");
        response = ops_maintenance_config.update_data_push_maintenance_config(R"({"enabled":true,"interval_seconds":120,"unhealthy_after_seconds":45})");
        expect_eq_int(response.status_code, 200, "operations data push maintenance re-enable config");
        navcaster::http_api::OperationsController ops_maintenance_interval(self_redis, 7150);
        response = ops_maintenance_interval.scheduled_data_push_maintenance("202606", 7100);
        expect_eq_int(response.status_code, 200, "operations scheduled data push maintenance interval ok");
        response_body = nlohmann::json::parse(response.body);
        expect_true(!response_body.value("executed", true), "operations scheduled data push maintenance skipped by interval");
        expect_eq(response_body.value("skip_reason", std::string{}), "interval", "operations scheduled data push maintenance interval reason");
        self_redis.hdel(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str());
        response = ops_self.update_data_push_job_control("self-relay-push-job", "202606", R"({"action":"retry","period":"202606","operator_note":"admin retry for maintenance"})");
        expect_eq_int(response.status_code, 200, "operations retries relay data push for maintenance");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body.value("status", std::string{}), "queued", "operations retry for maintenance status");
        relay_record = self_redis.hget(navcaster::redis_keys::PUSH_RECORD, relay_uid.c_str());
        expect_true(relay_record.value("enabled", false), "operations retry for maintenance enables push record");
        self_redis.hset(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str(), nlohmann::json{{"uid", relay_uid}, {"state", 1}, {"connect_key", "relay-auto-running"}, {"node_uid", "node-auto"}, {"node_name", "Auto Node"}}.dump());
        navcaster::http_api::OperationsController ops_maintenance_running(self_redis, 7200);
        response = ops_maintenance_running.maintain_data_push_jobs_runtime("202606", 300);
        expect_eq_int(response.status_code, 200, "operations auto maintains running relay data push");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("failed_count", -1), 0, "operations auto running failed count");
        stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_eq(stored_relay_job.value("status", std::string{}), "running", "operations auto running status");
        expect_eq(stored_relay_job.value("relay_status", std::string{}), "running", "operations auto relay running status");
        expect_eq(stored_relay_job.value("relay_connect_key", std::string{}), "relay-auto-running", "operations auto running connect key");
        expect_eq_int(stored_relay_job.value("runtime_maintenance_time", 0), 7200, "operations auto running maintenance timestamp");
        self_redis.hdel(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str());
        navcaster::http_api::OperationsController ops_maintenance_stopped(self_redis, 7210);
        response = ops_maintenance_stopped.maintain_data_push_jobs_runtime("202606", 300);
        expect_eq_int(response.status_code, 200, "operations auto maintains stopped relay data push before threshold");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("failed_count", -1), 0, "operations auto stopped before threshold failed count");
        stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_eq(stored_relay_job.value("status", std::string{}), "running", "operations auto stopped before threshold keeps business status");
        expect_eq(stored_relay_job.value("relay_status", std::string{}), "stopped", "operations auto stopped before threshold relay status");
        expect_eq_int(stored_relay_job.value("runtime_unhealthy_since", 0), 7210, "operations auto stopped starts unhealthy timer");
        navcaster::http_api::OperationsController ops_maintenance_failed(self_redis, 7520);
        response = ops_maintenance_failed.maintain_data_push_jobs_runtime("202606", 300);
        expect_eq_int(response.status_code, 200, "operations auto fails stopped relay data push after threshold");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("failed_count", 0), 1, "operations auto stopped after threshold failed count");
        stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_eq(stored_relay_job.value("status", std::string{}), "failed", "operations auto stopped after threshold status");
        expect_eq(stored_relay_job.value("failure_reason", std::string{}), "relay_runtime_missing", "operations auto stopped after threshold reason");
        expect_eq_int(stored_relay_job.value("failure_time", 0), 7520, "operations auto stopped failure time");
        expect_eq_int(stored_relay_job.value("runtime_failure_after_seconds", 0), 300, "operations auto stopped failure threshold");
        relay_record = self_redis.hget(navcaster::redis_keys::PUSH_RECORD, relay_uid.c_str());
        expect_true(!relay_record.value("enabled", true), "operations auto stopped after threshold disables push record");
        navcaster::http_api::OperationsController ops_manual_retry(self_redis, 7525);
        response = ops_manual_retry.update_data_push_job_control("self-relay-push-job", "202606", R"({"action":"retry","period":"202606","operator_note":"admin retry for manual maintenance"})");
        expect_eq_int(response.status_code, 200, "operations retries relay data push for manual maintenance");
        self_redis.hdel(navcaster::redis_keys::PUSH_STAT, relay_uid.c_str());
        navcaster::http_api::OperationsController ops_manual_maintenance(self_redis, 7530);
        response = ops_manual_maintenance.run_data_push_maintenance("202606", R"({"period":"202606","unhealthy_after_seconds":0})");
        expect_eq_int(response.status_code, 200, "operations manual data push maintenance override threshold");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.value("manual", false), "operations manual data push maintenance marks manual");
        expect_eq_int(response_body.value("failed_count", 0), 1, "operations manual data push maintenance failed count");
        expect_eq_int(response_body.value("unhealthy_after_seconds", -1), 0, "operations manual data push maintenance threshold override");
        stored_relay_job = self_redis.hget(navcaster::redis_keys::data_push_job("202606").c_str(), "self-relay-push-job");
        expect_eq(stored_relay_job.value("status", std::string{}), "failed", "operations manual data push maintenance fails immediately");
        expect_eq_int(stored_relay_job.value("runtime_failure_after_seconds", -1), 0, "operations manual data push maintenance stored threshold");
        response = self_service.append_data_push_usage(user_subject, R"({"usage_id":"self-overdraw","target_mountpoint":"SELFBASE","actual_debit_cents":200,"period":"202606"})");
        expect_eq_int(response.status_code, 409, "self service data push rejects insufficient balance");
        expect_true(self_redis.hget(navcaster::redis_keys::data_push("202606").c_str(), "self-overdraw").is_null(), "self service data push insufficient no usage write");
        response = self_service.append_data_push_usage(supplier_subject, R"({"usage_id":"supplier-data-push","target_mountpoint":"SELFBASE","actual_debit_cents":1,"period":"202606"})");
        expect_eq_int(response.status_code, 403, "self service supplier cannot use me data push");
        response = self_service.create_data_push_job(supplier_subject, R"({"config_id":"self-push-cfg","used_seconds":1,"period":"202606"})");
        expect_eq_int(response.status_code, 403, "self service supplier cannot create me data push job");

        expect_true(domain_repo.upsert_station_record({
            {"mountpoint", "SELFBASE"},
            {"station_id", "self-station-record"},
            {"last_supplier_account_id", "self-supplier"},
        }, 7030).status == navcaster::storage::RepositoryStatus::Ok, "self service station fixture");
        expect_true(domain_repo.append_supplier_supply_usage({
            {"usage_id", "self-supply"},
            {"supplier_account_id", "self-supplier"},
            {"access_account_id", "self-station"},
            {"mountpoint", "SELFBASE"},
            {"used_seconds", 120},
            {"earning_cents", 25},
        }, "202606", 7031).status == navcaster::storage::RepositoryStatus::Ok, "self service supply fixture");
        response = self_service.supplier_stations(supplier_subject);
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("SELFBASE"), "self service supplier station filtered");
        response = self_service.supplier_supply_usage(supplier_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains("self-supply"), "self service supplier usage filtered");
        response = self_service.supplier_earnings(supplier_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("total_earning_cents", 0), 25, "self service supplier earnings total");
        expect_eq_int(response_body.value("total_supply_seconds", 0), 120, "self service supplier earnings seconds");
        expect_eq_int(response_body.value("pending_earning_cents", 0), 25, "self service supplier earnings pending before settlement");
        expect_eq_int(response_body.value("settled_earning_cents", 0), 0, "self service supplier earnings settled before settlement");
        expect_eq_int(response_body.value("settlement_count", 0), 0, "self service supplier earnings settlement count before settlement");
        auto self_settlement = domain_repo.create_supplier_settlement({
            {"supplier_account_id", "self-supplier"},
            {"period", "202606"},
            {"operator_note", "self settlement"},
        }, "202606", 7032);
        expect_true(self_settlement.status == navcaster::storage::RepositoryStatus::Ok, "self service fixture supplier settlement");
        response = self_service.supplier_settlements(supplier_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_true(response_body.contains(self_settlement.id), "self service supplier settlements filtered");
        expect_eq(response_body[self_settlement.id].value("status", std::string{}), "pending_payment", "self service settlement pending payment");
        response = self_service.supplier_supply_usage(supplier_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_eq(response_body["self-supply"].value("status", std::string{}), "settled", "self service supplier usage settled after settlement");
        response = self_service.supplier_earnings(supplier_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("pending_earning_cents", 0), 0, "self service supplier earnings pending after settlement");
        expect_eq_int(response_body.value("settled_earning_cents", 0), 25, "self service supplier earnings settled after settlement");
        expect_eq_int(response_body.value("pending_payment_cents", 0), 25, "self service supplier earnings pending payment after settlement");
        expect_eq_int(response_body.value("paid_earning_cents", 0), 0, "self service supplier earnings paid before payment");
        expect_eq_int(response_body.value("settlement_count", 0), 1, "self service supplier earnings settlement count after settlement");
        auto self_payment = domain_repo.update_supplier_settlement_payment(self_settlement.id, "self-supplier", "202606", {{"status", "paid"}, {"payment_ref", "PAY-SELF"}}, 7033);
        expect_true(self_payment.status == navcaster::storage::RepositoryStatus::Ok, "self service fixture supplier payment");
        response = self_service.supplier_earnings(supplier_subject, "202606");
        response_body = nlohmann::json::parse(response.body);
        expect_eq_int(response_body.value("pending_payment_cents", 0), 0, "self service supplier earnings pending payment after paid");
        expect_eq_int(response_body.value("paid_earning_cents", 0), 25, "self service supplier earnings paid after payment");

        response = self_service.delete_access_account(user_subject, "me", "self-aacc");
        expect_eq_int(response.status_code, 200, "self service delete own access account");
        expect_eq(self_redis.hget(navcaster::redis_keys::AACC_USERNAME, "self-rover").value("status", std::string{}), "deleted", "self service access username tombstone");
    }

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
    account_controller_redis.hset(navcaster::redis_keys::act_session("session-user").c_str(), "active-session", nlohmann::json{{"uid", "active-session"}, {"connect_key", "active-session"}, {"account", "session-user"}}.dump());
    account_response = account_controller.get_account("active");
    expect_eq_int(account_response.status_code, 200, "account controller active special get ok");
    account_controller_body = nlohmann::json::parse(account_response.body);
    expect_true(account_controller_body.contains("session-user"), "account controller active special reads sessions");
    expect_true(account_controller_body.contains("active-session"), "account controller active special reads ACT_SESSION");
    account_response = account_controller.get_account("");
    expect_eq_int(account_response.status_code, 200, "account controller empty get lists active sessions");
    account_controller_body = nlohmann::json::parse(account_response.body);
    expect_true(account_controller_body.contains("active-session"), "account controller empty get uses active sessions");
    account_response = account_controller.list_active_sessions();
    expect_eq_int(account_response.status_code, 200, "account controller list active sessions ok");
    account_controller_body = nlohmann::json::parse(account_response.body);
    expect_true(account_controller_body.contains("active-session"), "account controller list active sessions uses ACT_SESSION");
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

    statistics_controller_response = statistics_controller.overview({{"start", "0"}, {"end", "3601"}});
    expect_eq_int(statistics_controller_response.status_code, 200, "statistics controller explicit epoch range status");
    statistics_controller_body = nlohmann::json::parse(statistics_controller_response.body);
    expect_eq_int(statistics_controller_body.value("start", -1), 0, "statistics controller preserves explicit epoch start");
    expect_eq_int(statistics_controller_body.value("mpt_connections", 0), 1, "statistics controller explicit epoch reads mpt logs");

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
    auth_sse_redis.hset(navcaster::redis_keys::STR_ACTIVE_LEGACY, "sse-conflict", nlohmann::json{{"account", "legacy-acct"}}.dump());
    auth_sse_redis.hset(navcaster::redis_keys::act_session("acct-1").c_str(), "acct-1-conn", nlohmann::json{{"uid", "acct-1-conn"}, {"connect_key", "acct-1-conn"}, {"account", "acct-1"}}.dump());
    auth_sse_redis.hset(navcaster::redis_keys::act_session("acct-1").c_str(), "sse-conflict", nlohmann::json{{"uid", "sse-conflict"}, {"connect_key", "sse-conflict"}, {"account", "session-acct"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::ACT_RECORD, "wrong-redis", nlohmann::json{{"account", "wrong-redis"}}.dump());
    caster_sse_redis.hset(navcaster::redis_keys::act_session("wrong-redis").c_str(), "wrong-session", nlohmann::json{{"account", "wrong-redis"}}.dump());
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
    auto sse_account_actives = sse_snapshots.account_actives();
    expect_true(sse_account_actives.contains("acct-1"), "sse snapshot account actives keeps legacy fallback");
    expect_true(sse_account_actives.contains("acct-1-conn"), "sse snapshot account actives reads ACT_SESSION");
    expect_true(!sse_account_actives.contains("wrong-session"), "sse snapshot account actives ignores caster redis");
    expect_eq(sse_account_actives["sse-conflict"].value("account", std::string{}), "session-acct", "sse snapshot account actives prefers ACT_SESSION");

    navcaster::http_api::AccountController admin_online_controller(auth_sse_redis, 12000);
    auto admin_online_response = admin_online_controller.list_active_sessions();
    expect_eq_int(admin_online_response.status_code, 200, "admin online v1 source controller status");
    auto admin_online_body = nlohmann::json::parse(admin_online_response.body);
    expect_true(admin_online_body.contains("acct-1"), "admin online v1 source keeps STR_ACTIVE fallback");
    expect_true(admin_online_body.contains("acct-1-conn"), "admin online v1 source reads ACT_SESSION");
    expect_true(!admin_online_body.contains("wrong-session"), "admin online v1 source ignores caster redis");
    expect_eq(admin_online_body["sse-conflict"].value("account", std::string{}), "session-acct", "admin online v1 source prefers ACT_SESSION");

    FakeRedisHashClient admin_audit_redis;
    navcaster::http_api::AuditLogService admin_audit_service(admin_audit_redis);
    admin_audit_redis.lists[navcaster::redis_keys::LOG_AUDIT].push_back({{"id", 10}, {"actor", "admin"}, {"action", "PUT /api/v1/admin/accounts/acc-1"}, {"target_type", "accounts"}, {"target_id", "acc-1"}, {"result", 200}});
    admin_audit_redis.lists[navcaster::redis_keys::LOG_AUDIT].push_back({{"id", 9}, {"actor", "supplier"}, {"action", "POST /api/v1/supplier/access-accounts"}, {"target_type", "access-accounts"}, {"target_id", "aacc-1"}, {"result", 201}});
    auto admin_audit_response = admin_audit_service.list(10, 0, "admin", "PUT", "accounts");
    expect_eq_int(admin_audit_response.status_code, 200, "admin audit v1 source service status");
    auto admin_audit_body = nlohmann::json::parse(admin_audit_response.body);
    expect_eq_int(admin_audit_body.value("total", 0), 2, "admin audit v1 source reports total log size");
    expect_eq_int(admin_audit_body.value("next_cursor", 0), 2, "admin audit v1 source advances cursor by scanned entries");
    expect_true(!admin_audit_body.value("has_more", true), "admin audit v1 source detects end");
    expect_eq_int(static_cast<int>(admin_audit_body["items"].size()), 1, "admin audit v1 source filters item count");
    expect_eq(admin_audit_body["items"][0].value("target_id", std::string{}), "acc-1", "admin audit v1 source filters target");

    std::unordered_set<std::string> parsed_channels;
    bool wildcard_channels = false;
    parse_sse_channels("clients, streams ,account_actives", parsed_channels, wildcard_channels);
    expect_true(!wildcard_channels, "sse channels parser keeps explicit list non-wildcard");
    expect_true(parsed_channels.contains("clients"), "sse channels parser reads clients");
    expect_true(parsed_channels.contains("streams"), "sse channels parser trims streams");
    expect_true(parsed_channels.contains("account_actives"), "sse channels parser reads account actives");
    expect_true(!parsed_channels.contains("servers"), "sse channels parser excludes unsubscribed channel");

    SseClient explicit_sse_client;
    explicit_sse_client.channels = parsed_channels;
    explicit_sse_client.wildcard = wildcard_channels;
    expect_true(sse_client_subscribes_to(explicit_sse_client, "clients"), "sse explicit client subscribes to listed channel");
    expect_true(!sse_client_subscribes_to(explicit_sse_client, "servers"), "sse explicit client skips unlisted channel");

    std::vector<SseClient> sse_clients{explicit_sse_client};
    expect_true(sse_channel_has_subscriber(sse_clients, "streams"), "sse channel subscriber lookup finds subscribed channel");
    expect_true(!sse_channel_has_subscriber(sse_clients, "servers"), "sse channel subscriber lookup skips unsubscribed channel");

    parse_sse_channels("*", parsed_channels, wildcard_channels);
    SseClient wildcard_sse_client;
    wildcard_sse_client.channels = parsed_channels;
    wildcard_sse_client.wildcard = wildcard_channels;
    expect_true(wildcard_sse_client.wildcard, "sse channels parser preserves wildcard");
    expect_true(sse_client_subscribes_to(wildcard_sse_client, "servers"), "sse wildcard client subscribes to any channel");
    sse_clients.push_back(wildcard_sse_client);
    expect_true(sse_channel_has_subscriber(sse_clients, "servers"), "sse channel subscriber lookup honors wildcard");

    parse_sse_channels(" , ", parsed_channels, wildcard_channels);
    expect_true(wildcard_channels, "sse empty channel list falls back to wildcard");

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

    auto observed_master = navcaster::core::MasterLeaseService::observe_master("", "node-a", "node-a");
    expect_true(observed_master.changed, "master lease observes first master change");
    expect_true(observed_master.is_self, "master lease observes self master");
    expect_eq(observed_master.current_master_id, "node-a", "master lease observed self id");
    observed_master = navcaster::core::MasterLeaseService::observe_master("node-a", "node-b", "node-a");
    expect_true(observed_master.changed, "master lease observes remote master change");
    expect_true(!observed_master.is_self, "master lease remote master is not self");
    expect_eq(observed_master.current_master_id, "node-b", "master lease observed remote id");
    observed_master = navcaster::core::MasterLeaseService::observe_master("node-b", "node-b", "node-a");
    expect_true(!observed_master.changed, "master lease unchanged observed master");

    auto lease_plan = navcaster::core::MasterLeaseService::apply_keepalive_result(false, true, "node-a", 1234);
    expect_eq_int(static_cast<int>(lease_plan.event), static_cast<int>(navcaster::core::MasterLeaseEventType::Acquired), "master lease acquired event");
    expect_true(lease_plan.is_master, "master lease acquired sets master");
    expect_true(lease_plan.trigger_cluster_sync, "master lease acquired triggers sync");
    expect_eq(lease_plan.log_key, navcaster::redis_keys::log_node("node-a"), "master lease acquired log key");
    expect_eq(lease_plan.log_field, "1234_master_acquired", "master lease acquired log field");
    expect_eq(lease_plan.payload.value("event", std::string{}), "master_acquired", "master lease acquired payload event");
    expect_eq(lease_plan.payload.value("node_id", std::string{}), "node-a", "master lease acquired payload node");
    expect_eq_int(lease_plan.payload.value("timestamp", 0), 1234, "master lease acquired payload timestamp");

    lease_plan = navcaster::core::MasterLeaseService::apply_keepalive_result(true, true, "node-a", 1235);
    expect_eq_int(static_cast<int>(lease_plan.event), static_cast<int>(navcaster::core::MasterLeaseEventType::None), "master lease renewed no event");
    expect_true(lease_plan.is_master, "master lease renewed keeps master");
    expect_true(lease_plan.trigger_cluster_sync, "master lease renewed triggers sync");
    expect_true(lease_plan.log_key.empty(), "master lease renewed no log key");

    lease_plan = navcaster::core::MasterLeaseService::apply_keepalive_result(true, false, "node-a", 1236);
    expect_eq_int(static_cast<int>(lease_plan.event), static_cast<int>(navcaster::core::MasterLeaseEventType::Lost), "master lease lost event");
    expect_true(!lease_plan.is_master, "master lease lost clears master");
    expect_true(!lease_plan.trigger_cluster_sync, "master lease lost does not trigger sync");
    expect_eq(lease_plan.log_key, navcaster::redis_keys::log_node("node-a"), "master lease lost log key");
    expect_eq(lease_plan.log_field, "1236_master_lost", "master lease lost log field");
    expect_eq(lease_plan.payload.value("event", std::string{}), "master_lost", "master lease lost payload event");
    expect_eq(lease_plan.payload.value("node_id", std::string{}), "node-a", "master lease lost payload node");
    expect_eq_int(lease_plan.payload.value("timestamp", 0), 1236, "master lease lost payload timestamp");

    lease_plan = navcaster::core::MasterLeaseService::apply_keepalive_result(false, false, "node-a", 1237);
    expect_eq_int(static_cast<int>(lease_plan.event), static_cast<int>(navcaster::core::MasterLeaseEventType::None), "master lease follower failed no event");
    expect_true(!lease_plan.is_master, "master lease follower failed stays follower");
    expect_true(!lease_plan.trigger_cluster_sync, "master lease follower failed no sync");

    navcaster::core::PullRecordMap pull_records;
    navcaster::core::PullStatusMap pull_statuses;
    navcaster::core::RelayDistributedMap pull_distributed;
    pull_records.emplace("pull-new", make_pull_schedule_record("pull-new", true, "v1"));
    pull_records.emplace("pull-disabled", make_pull_schedule_record("pull-disabled", false, "v1"));
    auto pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    expect_eq_int(static_cast<int>(pull_actions.size()), 1, "relay scheduler pull active count");
    expect_eq_int(static_cast<int>(pull_actions[0].message.type), static_cast<int>(caster::core::BOARDCAST_TYPE_PULL_OPERATE), "relay scheduler pull active type");
    expect_eq_int(static_cast<int>(pull_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_ACTIVE), "relay scheduler pull active operate");
    expect_eq(pull_actions[0].message.target, "pull-new", "relay scheduler pull active target");
    expect_eq(pull_actions[0].message.reason_str, "Pull Task Active", "relay scheduler pull active reason");
    expect_true(!pull_distributed.contains("pull-new"), "relay scheduler plan does not mutate active distribution");
    navcaster::core::RelayScheduler::apply_distributed_mutation(pull_actions[0], pull_distributed);
    expect_true(pull_distributed.contains("pull-new"), "relay scheduler records active distribution");
    expect_true(!pull_distributed.contains("pull-disabled"), "relay scheduler skips disabled inactive record");

    pull_statuses.emplace("pull-new", make_pull_schedule_status("pull-new"));
    pull_distributed["pull-new"] = pull_records.at("pull-new").toString();
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    expect_eq_int(static_cast<int>(pull_actions.size()), 0, "relay scheduler pull unchanged no action");
    pull_records.insert_or_assign("pull-new", make_pull_schedule_record("pull-new", true, "v2"));
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    expect_eq_int(static_cast<int>(pull_actions.size()), 1, "relay scheduler pull changed count");
    expect_eq_int(static_cast<int>(pull_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_INACTIVE), "relay scheduler pull changed inactive");
    expect_eq(pull_actions[0].message.reason_str, "Pull Task Config Changed", "relay scheduler pull changed reason");
    expect_true(pull_distributed.contains("pull-new"), "relay scheduler plan does not erase changed distribution");
    navcaster::core::RelayScheduler::apply_distributed_mutation(pull_actions[0], pull_distributed);
    expect_true(!pull_distributed.contains("pull-new"), "relay scheduler pull changed clears distributed");
    pull_statuses.erase("pull-new");
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    expect_eq_int(static_cast<int>(pull_actions.size()), 1, "relay scheduler pull changed restarts after status clears");
    expect_eq_int(static_cast<int>(pull_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_ACTIVE), "relay scheduler pull changed restart active");
    expect_eq(pull_actions[0].message.reason_str, "Pull Task Active", "relay scheduler pull changed restart reason");
    navcaster::core::RelayScheduler::apply_distributed_mutation(pull_actions[0], pull_distributed);
    navcaster::core::RelayScheduler::apply_distributed_mutation(pull_actions[0], pull_distributed);
    expect_eq(pull_distributed.at("pull-new"), pull_records.at("pull-new").toString(), "relay scheduler store mutation idempotent");
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    expect_eq_int(static_cast<int>(pull_actions.size()), 1, "relay scheduler pull active retries until status appears");
    expect_eq_int(static_cast<int>(pull_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_ACTIVE), "relay scheduler pull retry active operate");
    pull_statuses.emplace("pull-new", make_pull_schedule_status("pull-new"));
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    expect_eq_int(static_cast<int>(pull_actions.size()), 0, "relay scheduler pull repeated active suppressed after status");

    pull_statuses.emplace("pull-orphan", make_pull_schedule_status("pull-orphan"));
    pull_distributed["pull-orphan"] = "old";
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    bool found_pull_orphan = false;
    for (const auto &action : pull_actions)
    {
        if (action.message.target == "pull-orphan")
        {
            found_pull_orphan = true;
            expect_eq(action.message.reason_str, "Pull Task Inactive", "relay scheduler pull orphan reason");
            expect_true(pull_distributed.contains("pull-orphan"), "relay scheduler plan does not erase orphan distribution");
            navcaster::core::RelayScheduler::apply_distributed_mutation(action, pull_distributed);
        }
    }
    expect_true(found_pull_orphan, "relay scheduler pull orphan inactive action");
    expect_true(!pull_distributed.contains("pull-orphan"), "relay scheduler pull orphan clears distributed");
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    found_pull_orphan = false;
    for (const auto &action : pull_actions)
    {
        if (action.message.target == "pull-orphan")
        {
            found_pull_orphan = true;
            expect_eq(action.message.reason_str, "Pull Task Inactive", "relay scheduler pull orphan retry reason");
        }
    }
    expect_true(found_pull_orphan, "relay scheduler pull orphan inactive retries while status remains");
    pull_statuses.erase("pull-orphan");
    pull_actions = navcaster::core::RelayScheduler::plan_pull_distribution(pull_records, pull_statuses, pull_distributed);
    bool found_cleared_pull_orphan = false;
    for (const auto &action : pull_actions)
    {
        if (action.message.target == "pull-orphan")
        {
            found_cleared_pull_orphan = true;
        }
    }
    expect_true(!found_cleared_pull_orphan, "relay scheduler pull orphan cleared snapshot no action");

    navcaster::core::PushRecordMap push_records;
    navcaster::core::PushStatusMap push_statuses;
    navcaster::core::RelayDistributedMap push_distributed;
    push_records.emplace("push-new", make_push_schedule_record("push-new", true, "v1"));
    auto push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    expect_eq_int(static_cast<int>(push_actions.size()), 1, "relay scheduler push active count");
    expect_eq_int(static_cast<int>(push_actions[0].message.type), static_cast<int>(caster::core::BOARDCAST_TYPE_RUSH_OPERATE), "relay scheduler push active type");
    expect_eq_int(static_cast<int>(push_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_ACTIVE), "relay scheduler push active operate");
    expect_eq(push_actions[0].message.reason_str, "Push Task Active", "relay scheduler push active reason");
    navcaster::core::RelayScheduler::apply_distributed_mutation(push_actions[0], push_distributed);
    push_statuses.emplace("push-new", make_push_schedule_status("push-new"));
    push_distributed["push-new"] = push_records.at("push-new").toString();
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    expect_eq_int(static_cast<int>(push_actions.size()), 0, "relay scheduler push unchanged no action");
    push_records.insert_or_assign("push-new", make_push_schedule_record("push-new", true, "v2"));
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    expect_eq_int(static_cast<int>(push_actions.size()), 1, "relay scheduler push changed count");
    expect_eq_int(static_cast<int>(push_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_INACTIVE), "relay scheduler push changed inactive");
    expect_eq(push_actions[0].message.reason_str, "Push Task Config Changed", "relay scheduler push changed reason");
    expect_eq_int(static_cast<int>(push_actions[0].distributed_mutation), static_cast<int>(navcaster::core::RelayDistributedMutation::Erase), "relay scheduler push changed erase mutation");
    navcaster::core::RelayScheduler::apply_distributed_mutation(push_actions[0], push_distributed);
    navcaster::core::RelayScheduler::apply_distributed_mutation(push_actions[0], push_distributed);
    expect_true(!push_distributed.contains("push-new"), "relay scheduler erase mutation idempotent");
    push_statuses.erase("push-new");
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    expect_eq_int(static_cast<int>(push_actions.size()), 1, "relay scheduler push changed restarts after status clears");
    expect_eq_int(static_cast<int>(push_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_ACTIVE), "relay scheduler push changed restart active");
    expect_eq(push_actions[0].message.reason_str, "Push Task Active", "relay scheduler push changed restart reason");
    navcaster::core::RelayScheduler::apply_distributed_mutation(push_actions[0], push_distributed);
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    expect_eq_int(static_cast<int>(push_actions.size()), 1, "relay scheduler push active retries until status appears");
    expect_eq_int(static_cast<int>(push_actions[0].message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_ACTIVE), "relay scheduler push retry active operate");
    push_statuses.emplace("push-new", make_push_schedule_status("push-new"));
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    expect_eq_int(static_cast<int>(push_actions.size()), 0, "relay scheduler push repeated active suppressed after status");
    push_statuses.emplace("push-disabled", make_push_schedule_status("push-disabled"));
    push_records.emplace("push-disabled", make_push_schedule_record("push-disabled", false, "v1"));
    push_distributed["push-disabled"] = "old";
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    bool found_push_disabled = false;
    for (const auto &action : push_actions)
    {
        if (action.message.target == "push-disabled")
        {
            found_push_disabled = true;
            expect_eq_int(static_cast<int>(action.message.operate), static_cast<int>(caster::core::BOARDCAST_OPERATR_INACTIVE), "relay scheduler push disabled inactive");
            expect_eq(action.message.reason_str, "Push Task Inactive", "relay scheduler push disabled reason");
            navcaster::core::RelayScheduler::apply_distributed_mutation(action, push_distributed);
        }
    }
    expect_true(found_push_disabled, "relay scheduler push disabled action");
    expect_true(!push_distributed.contains("push-disabled"), "relay scheduler push disabled clears distributed");
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    found_push_disabled = false;
    for (const auto &action : push_actions)
    {
        if (action.message.target == "push-disabled")
        {
            found_push_disabled = true;
            expect_eq(action.message.reason_str, "Push Task Inactive", "relay scheduler push disabled retry reason");
        }
    }
    expect_true(found_push_disabled, "relay scheduler push disabled inactive retries while status remains");
    push_statuses.erase("push-disabled");
    push_actions = navcaster::core::RelayScheduler::plan_push_distribution(push_records, push_statuses, push_distributed);
    bool found_cleared_push_disabled = false;
    for (const auto &action : push_actions)
    {
        if (action.message.target == "push-disabled")
        {
            found_cleared_push_disabled = true;
        }
    }
    expect_true(!found_cleared_push_disabled, "relay scheduler push disabled cleared snapshot no action");

    navcaster::core::NodeHistoryRecorder node_recorder("node-hist");
    nlohmann::json node_sample = {
        {"cpu_usage", 12.0},
        {"mem_usage", 100.0},
        {"server_count", 2},
        {"client_count", 3},
        {"pull_count", 1},
        {"push_count", 4},
        {"connect_count", 5},
        {"send_speed", 7.0},
        {"recv_speed", 9.0},
        {"send_total", 1000LL},
        {"recv_total", 2000LL},
        {"queue_delay", 11.0},
    };
    auto history_writes = node_recorder.record(node_sample, 100);
    expect_eq_int(static_cast<int>(history_writes.size()), 1, "node history first write raw only");
    expect_eq(history_writes[0].key, navcaster::redis_keys::node_history("node-hist"), "node history raw key");
    expect_eq_int(history_writes[0].trim_max, navcaster::core::NODE_HISTORY_RAW_TRIM_MAX, "node history raw trim");
    auto raw_snapshot = nlohmann::json::parse(history_writes[0].value);
    expect_eq_int(raw_snapshot.value("mpt", 0), 2, "node history raw server count");
    expect_eq_int(raw_snapshot.value("usr", 0), 3, "node history raw client count");
    expect_eq_int(raw_snapshot.value("pull", 0), 1, "node history raw pull count");
    expect_eq_int(raw_snapshot.value("push", 0), 4, "node history raw push count");
    expect_eq_int(raw_snapshot.value("conn", 0), 5, "node history raw connect count");
    expect_true(raw_snapshot.value("cpu", 0.0) == 12.0, "node history raw cpu");
    expect_true(raw_snapshot.value("q_delay", 0.0) == 11.0, "node history raw delay");

    for (int i = 1; i < 11; ++i)
    {
        node_sample["cpu_usage"] = 12.0 + i;
        node_sample["send_total"] = 1000LL + i;
        node_sample["recv_total"] = 2000LL + i;
        history_writes = node_recorder.record(node_sample, 100 + i);
        expect_eq_int(static_cast<int>(history_writes.size()), 1, "node history pre aggregate raw only");
    }
    node_sample["cpu_usage"] = 23.0;
    node_sample["send_total"] = 1011LL;
    node_sample["recv_total"] = 2011LL;
    history_writes = node_recorder.record(node_sample, 111);
    expect_eq_int(static_cast<int>(history_writes.size()), 2, "node history 12th emits 1m");
    expect_eq(history_writes[1].key, navcaster::redis_keys::node_history_1m("node-hist"), "node history 1m key");
    expect_eq_int(history_writes[1].trim_max, navcaster::core::NODE_HISTORY_1M_TRIM_MAX, "node history 1m trim");
    auto one_minute = nlohmann::json::parse(history_writes[1].value);
    expect_eq_int(one_minute.value("t", 0), 105, "node history 1m avg time");
    expect_true(one_minute.value("cpu", 0.0) == 17.5, "node history 1m avg cpu");
    expect_eq_int(one_minute.value("send_total", 0), 1011, "node history 1m last send total");
    expect_eq_int(one_minute.value("recv_total", 0), 2011, "node history 1m last recv total");

    for (int minute = 1; minute < 5; ++minute)
    {
        for (int sample = 0; sample < 12; ++sample)
        {
            node_sample["cpu_usage"] = 20.0 + minute;
            history_writes = node_recorder.record(node_sample, 200 + minute * 12 + sample);
        }
    }
    expect_eq_int(static_cast<int>(history_writes.size()), 3, "node history fifth 1m emits 5m");
    expect_eq(history_writes[2].key, navcaster::redis_keys::node_history_5m("node-hist"), "node history 5m key");
    expect_eq_int(history_writes[2].trim_max, navcaster::core::NODE_HISTORY_5M_TRIM_MAX, "node history 5m trim");
    auto five_minute = nlohmann::json::parse(history_writes[2].value);
    expect_true(five_minute.contains("cpu"), "node history 5m aggregate body");

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
