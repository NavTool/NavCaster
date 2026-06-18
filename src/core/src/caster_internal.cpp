#include "caster_internal.h"
#include <chrono>
#include <cmath>
#include <list>
#include <memory>
#include <set>
// #include <format>
#include <spdlog/spdlog.h>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "knt.h"
#include "SysUsage.h"
#include "access_policy_service.h"
#include "core_result.h"
#include "node_history_recorder.h"
#include "relay_scheduler.h"
#include "master_lease_service.h"
#include "source_table_service.h"
#include "version.h"

#define __class__ "caster_internal"

namespace
{
using navcaster::core::normalize_access_group_uid;
using navcaster::core::CoreErrorCode;
using navcaster::core::CoreResult;

long long current_process_id()
{
#ifdef _WIN32
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(getpid());
#endif
}

template <typename StatusMap>
size_t count_running_relay_statuses(const StatusMap &statuses, const std::string &node_id = std::string())
{
    size_t count = 0;
    for (const auto &item : statuses)
    {
        if (!item.second.running())
        {
            continue;
        }
        if (!node_id.empty() && item.second.node_uid() != node_id)
        {
            continue;
        }
        count++;
    }
    return count;
}

CoreResult require_publish_context(redisAsyncContext *context, const char *operation, const std::string &redis_key, const std::string &subject = {})
{
    if (context == nullptr)
    {
        return CoreResult::failure(CoreErrorCode::RedisDisconnected,
                                   operation,
                                   "Redis publish context is not connected")
            .with_redis_key(redis_key)
            .with_subject(subject);
    }
    if (context->err)
    {
        return CoreResult::failure(CoreErrorCode::RedisError,
                                   operation,
                                   "Redis publish context has an error")
            .with_redis_key(redis_key)
            .with_subject(subject);
    }
    return CoreResult::success(operation);
}

const char *safe_cstr(const char *value)
{
    return value ? value : "";
}

int finish_redis_publish_failure(const CoreResult &result)
{
    spdlog::warn("[{}]: {}", __class__, result.summary());
    return navcaster::core::to_legacy_int(result, REDIS_ERR);
}
}

/*
    设计的的Redis表和频道组成
    初版：
    存储键值：
    1、基站登录记录表(HASH)    key:MPT:REC:mount        field:connectkey    value:updatetime
    2、基站在线记录表(HASH)    key:MPT:LIST             field:mount         value:updatetime
    3、基站详细信息表(HASH)    key:MPT:INFO:mount       field:connectkey    value:jsonstring
    4、基站地理信息表 (GEO)    key:MPT:GEO              field:connectkey    value:lat,lon
    5、基站订阅用户表(HSAH)    key:MPT:SUB:mount        field:connectkey    value:updatetime

    1、用户
    2、用户
    3、用户
    4、用户
    5、用户

    发布订阅：
    1、公共频道       CASTER:BROADCAST
    2、基站发布频道   MPT:mount
    3、用户发布频道   USR:username


*/

caster_internal::caster_internal()
{
    _node_history_recorder.set_node_id(_node_ID);
}

caster_internal::~caster_internal()
{
}

caster_internal *caster_internal::getInstance()
{
    static caster_internal *instance = new caster_internal();
    return instance;
}

int caster_internal::init(CasterCoreOpt opt, event_base *base)
{
    _update_intv = opt.update_intv();
    _key_expire_time = opt.key_expire_time();
    // 仅当配置项显式提供(>0)时覆盖默认阈值；0 表示沿用默认值(50m)
    if (opt.near_switch_distance() > 0.0)
        _near_switch_distance = opt.near_switch_distance();

    _upload_base_stat = opt.upload_base_stat();
    _upload_rover_stat = opt.upload_rover_stat();

    _base_enable_mult = opt.base_enable_mult();
    _base_keep_early = opt.base_keep_early();

    _rover_enable_mult = opt.rover_enable_mult();
    _rover_keep_early = opt.rover_keep_early();

    _notify_base_inactive = opt.base_notify_inactive();
    _notify_rover_inactive = opt.rover_noify_inactive();

    _redis_IP = opt.redis_host();
    _redis_port = opt.redis_port();
    _redis_Requirepass = opt.redis_password();

    _base = base;

    return 0;
}

void caster_internal::set_node_identity(const std::string &hostname, int listen_port, int http_port)
{
    _hostname = hostname;
    _listen_port = listen_port;
    _http_port = http_port;
    _process_id = current_process_id();

    // 由 hostname + listen_port + http_port 生成稳定 5 位 hex 标识
    // 同一实例只要监听端口不变, 重启后 ID 一致; 同实例多节点(不同端口) 也会得到不同 ID
    std::string seed = hostname + ":" + std::to_string(listen_port) + ":" + std::to_string(http_port);
    std::hash<std::string> hasher;
    size_t h = hasher(seed);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Node_%05X", static_cast<unsigned int>(h & 0xFFFFF));
    _node_ID = buf;
    _node_name = _node_ID;
    _node_history_recorder.set_node_id(_node_ID);
    spdlog::info("[caster_internal::set_node_identity]: host={} listen={} http={} -> {} (pid={})",
                 hostname, listen_port, http_port, _node_ID, _process_id);
}

int caster_internal::start()
{

    _startup_time = util_get_now_second();

    // 启动前清理崩溃残留的历史记录
    cleanup_stale_history();

    pubAttemptReconnect();
    subAttemptReconnect();

    _timeout_tv.tv_sec = _update_intv;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);
    event_add(_timeout_ev, &_timeout_tv);

    _testdelay_ev = event_new(_base, -1, EV_PERSIST, TestDelayCallback, this);

    return 0;
}

int caster_internal::stop()
{
    // 优雅停机: 为所有在线会话写入断开记录
    flush_online_history();

    redisAsyncDisconnect(_sub_context);
    redisAsyncDisconnect(_pub_context);
    return 0;
}

void caster_internal::flush_online_history()
{
    auto now = util_get_now_second();
    size_t base_count = _base_history_map.size();
    size_t rover_count = _rover_history_map.size();

    for (auto &[ck, entry] : _base_history_map)
    {
        entry["disconnect_time"] = now;
        entry["last_update"] = now;
        auto stt = _stream_status_map.find(ck);
        if (stt != _stream_status_map.end())
        {
            entry["send_total"] = stt->second.getSendTotal();
            entry["recv_total"] = stt->second.getRecvTotal();
        }
        std::string mount = entry.value("name", std::string());
        std::string field = std::to_string(entry.value("connect_time", 0LL)) + "_" + ck;
        std::string key = std::string(LOG_MPT_PREFIX) + mount;
        redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", key.c_str(), field.c_str(), entry.dump().c_str());
    }
    _base_history_map.clear();

    for (auto &[ck, entry] : _rover_history_map)
    {
        entry["disconnect_time"] = now;
        entry["last_update"] = now;
        auto stt = _stream_status_map.find(ck);
        if (stt != _stream_status_map.end())
        {
            entry["send_total"] = stt->second.getSendTotal();
            entry["recv_total"] = stt->second.getRecvTotal();
        }
        std::string user = entry.value("name", std::string());
        std::string field = std::to_string(entry.value("connect_time", 0LL)) + "_" + ck;
        std::string key = std::string(LOG_USR_PREFIX) + user;
        redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", key.c_str(), field.c_str(), entry.dump().c_str());
    }
    _rover_history_map.clear();

    if (base_count > 0 || rover_count > 0)
    {
        spdlog::info("[caster_internal::flush_online_history]: Flushed {} base + {} rover disconnect records", base_count, rover_count);
    }

    // 写入节点下线事件
    if (!_node_ID.empty() && _pub_context)
    {
        json node_event;
        node_event["event"] = "stop";
        node_event["node_id"] = _node_ID;
        node_event["node_name"] = _node_name;
        node_event["timestamp"] = now;
        std::string node_key = std::string(LOG_NODE_PREFIX) + _node_ID;
        std::string node_field = std::to_string(now) + "_stop";
        redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s",
                          node_key.c_str(), node_field.c_str(), node_event.dump().c_str());
    }
}

void caster_internal::cleanup_stale_history()
{
    // 使用同步 Redis 连接，在启动阶段补偿崩溃未写入的断开记录
    struct timeval tv = {2, 0};
    redisContext *ctx = redisConnectWithTimeout(_redis_IP.c_str(), _redis_port, tv);
    if (!ctx || ctx->err)
    {
        spdlog::warn("[caster_internal::cleanup_stale_history]: Cannot connect to Redis for startup cleanup: {}",
                     ctx ? ctx->errstr : "null context");
        if (ctx)
            redisFree(ctx);
        return;
    }

    if (!_redis_Requirepass.empty())
    {
        auto *reply = static_cast<redisReply *>(redisCommand(ctx, "AUTH %s", _redis_Requirepass.c_str()));
        if (reply)
            freeReplyObject(reply);
    }

    auto now = util_get_now_second();
    int compensated = 0;

    // 工具 lambda: 扫描指定前缀的 hash key, 补偿崩溃未写入断开时间的会话记录
    auto scan_and_compensate = [&](const char *prefix) {
        std::string match = std::string(prefix) + "*";
        unsigned long long cursor = 0;
        do
        {
            auto *sreply = static_cast<redisReply *>(
                redisCommand(ctx, "SCAN %llu MATCH %s COUNT 200 TYPE hash", cursor, match.c_str()));
            if (!sreply)
                break;
            if (sreply->type == REDIS_REPLY_ARRAY && sreply->elements == 2)
            {
                cursor = std::strtoull(sreply->element[0]->str, nullptr, 10);
                auto *arr = sreply->element[1];
                for (size_t i = 0; i < arr->elements; i++)
                {
                    if (!arr->element[i]->str)
                        continue;
                    std::string key = arr->element[i]->str;
                    auto *hreply = static_cast<redisReply *>(redisCommand(ctx, "HGETALL %s", key.c_str()));
                    if (hreply && hreply->type == REDIS_REPLY_ARRAY)
                    {
                        for (size_t j = 0; j + 1 < hreply->elements; j += 2)
                        {
                            std::string field = hreply->element[j]->str ? hreply->element[j]->str : "";
                            std::string value = hreply->element[j + 1]->str ? hreply->element[j + 1]->str : "";
                            try
                            {
                                auto entry = json::parse(value);
                                if (entry.contains("disconnect_time") &&
                                    entry["disconnect_time"].get<long long>() == 0 &&
                                    entry.contains("last_update"))
                                {
                                    long long last_update = entry["last_update"].get<long long>();
                                    if (now - last_update > _key_expire_time)
                                    {
                                        entry["disconnect_time"] = last_update;
                                        auto *r = static_cast<redisReply *>(
                                            redisCommand(ctx, "HSET %s %s %s", key.c_str(), field.c_str(), entry.dump().c_str()));
                                        if (r)
                                            freeReplyObject(r);
                                        compensated++;
                                    }
                                }
                            }
                            catch (...)
                            {
                            }
                        }
                    }
                    if (hreply)
                        freeReplyObject(hreply);
                }
            }
            else
            {
                freeReplyObject(sreply);
                break;
            }
            freeReplyObject(sreply);
        } while (cursor != 0);
    };

    scan_and_compensate(LOG_MPT_PREFIX);
    scan_and_compensate(LOG_USR_PREFIX);

    // 写入节点上线事件
    if (!_node_ID.empty())
    {
        json node_event;
        node_event["event"] = "start";
        node_event["node_id"] = _node_ID;
        node_event["node_name"] = _node_name;
        node_event["hostname"] = _hostname;
        node_event["listen_port"] = _listen_port;
        node_event["http_port"] = _http_port;
        node_event["process_id"] = _process_id;
        node_event["timestamp"] = now;
        std::string node_key = std::string(LOG_NODE_PREFIX) + _node_ID;
        std::string node_field = std::to_string(now) + "_start";
        auto *r = static_cast<redisReply *>(
            redisCommand(ctx, "HSET %s %s %s", node_key.c_str(), node_field.c_str(), node_event.dump().c_str()));
        if (r)
            freeReplyObject(r);
    }

    redisFree(ctx);

    if (compensated > 0)
    {
        spdlog::info("[caster_internal::cleanup_stale_history]: Compensated {} stale connection records (crash recovery)", compensated);
    }
    else
    {
        spdlog::info("[caster_internal::cleanup_stale_history]: No stale records found, clean startup");
    }
}

std::string caster_internal::get_status_str()
{

    std::string str = "Connection: " +
                      std::to_string(_server_connection_count + _client_connection_count) +
                      ", Server: " +
                      std::to_string(_server_connection_count) +
                      ", Client: " +
                      std::to_string(_client_connection_count) +
                      ", Pull: " +
                      std::to_string(_pull_connection_count) +
                      ", Push: " +
                      std::to_string(_push_connection_count);

    return str;

    // return std::format("Connection: {}, Active Server: {}, Active Client: {}", , _active_mount_set.size(), _active_user_set.size());
}

bool caster_internal::is_nearest_mpt(std::string mount_point) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.is_nearest_mount(mount_point);
}

bool caster_internal::is_alias_mpt(std::string mount_point)
{
    // 从alias映射表中查找
    return _alias_rule_map.find(mount_point) != _alias_rule_map.end();
}

std::string caster_internal::resolve_mount_group(const std::string &mount_point) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.resolve_mount_group(mount_point);
}

bool caster_internal::is_mount_inside_group(const std::string &group_uid, const std::string &mount_point) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.is_mount_inside_group(group_uid, mount_point);
}

bool caster_internal::check_nearest_mount_login(const std::string &group_uid, const std::string &mount_point, std::string *reason) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.check_nearest_mount_login(group_uid, mount_point, reason);
}

bool caster_internal::check_mount_visible(const std::string &group_uid, const std::string &mount_point, std::string *reason) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.check_mount_visible(group_uid, mount_point, reason);
}

bool caster_internal::check_mount_access(const std::string &group_uid, const std::string &mount_point, std::string *reason) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.check_mount_access(group_uid, mount_point, reason);
}

bool caster_internal::check_mount_nearby(const std::string &group_uid, const std::string &mount_point, std::string *reason) const
{
    navcaster::core::AccessPolicyService policy(_source_record_map, _source_decode_map, _access_group_map, _access_item_map);
    return policy.check_mount_nearby(group_uid, mount_point, reason);
}

int caster_internal::sub_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid, bool skip_access_check)
{
    try
    {
        // 自动识别别名挂载点：如果 channel 是别名，委托给 sub_alias_channel 处理
        if (is_alias_mpt(channel))
        {
            return sub_alias_channel(channel, user_name, connect_key, cb, arg, group_uid);
        }

        std::string deny_reason;
        if (!skip_access_check && !check_mount_access(normalize_access_group_uid(group_uid), channel, &deny_reason))
        {
            caster_reply Reply;
            Reply.type = CasterReply::ERR;
            Reply.str = deny_reason.c_str();
            cb(NULL, arg, &Reply);
            return 1;
        }

        if (_active_mount_map.find(channel) == _active_mount_map.end()) // 不是活跃频道
        {
            caster_reply Reply;
            Reply.type = CasterReply::ERR;
            Reply.str = "Can't Find Sub Base Recored";
            cb(NULL, arg, &Reply);
            return 1;
        }

        auto find = _base_sub_map.find(channel);
        if (find == _base_sub_map.end())
        {
            // 还没有订阅频道，添加订阅
            redisAsyncCommand(_sub_context, Redis_SUB_Base_Callback, this, "SUBSCRIBE MPT:%s", channel);
            std::unordered_map<std::string, caster_cb_item> channel_subs;
            _base_sub_map.insert(std::pair<std::string, std::unordered_map<std::string, caster_cb_item>>(channel, channel_subs));

            // 由于该频道是此节点的第一次订阅，因此发送一次激活函数
            send_status_base_channel(channel, "", CasterReply::ACTIVE, "First subscribe in one Caster Node");
        }
        find = _base_sub_map.find(channel);

        // 更新订阅者列表
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_SUBSCRIBE_LIST ":%s EX %s FIELDS 1 %s %s",
                          channel,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str());

        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.group_uid = normalize_access_group_uid(group_uid);
        cb_item.cb = cb;
        cb_item.arg = arg;
        find->second.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));

        // 更新用户订阅的挂载点信息
        auto item = _client_status_map.find(connect_key);
        if (item != _client_status_map.end())
        {
            item->second.set_alias_mpt(channel);
        }

        caster_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = channel;
        cb(NULL, arg, &Reply);
    }
    catch (const std::exception &e)
    {
        caster_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = e.what();
        cb(NULL, arg, &Reply);
    }

    return 0;
}

int caster_internal::sub_near_channel(const char *channel, const char *user_name, double lat, double lon, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    std::string deny_reason;
    if (!check_nearest_mount_login(normalize_access_group_uid(group_uid), channel, &deny_reason))
    {
        caster_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = deny_reason.c_str();
        cb(NULL, arg, &Reply);
        return 1;
    }

    // 查找是否是已经订阅过最近基站
    auto find = _base_near_sub_map.find(connect_key);
    if (find == _base_near_sub_map.end())
    {
        // 还没有订阅过，添加一条记录到map中
        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.group_uid = normalize_access_group_uid(group_uid);
        cb_item.cb = cb;
        cb_item.arg = arg;
        _base_near_sub_map.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));
    }
    else
    {
        // 已经订阅过，更新回调函数和参数
        find->second.group_uid = normalize_access_group_uid(group_uid);
        find->second.cb = cb;
        find->second.arg = arg;
    }

    if (lat == 0.0 && lon == 0.0)
    {
        spdlog::info("[{}:{}]: wait GGA before nearest query, mount [{}], user [{}], connect [{}]", __class__, __func__, channel, user_name, connect_key);
        caster_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = channel;
        cb(NULL, arg, &Reply);
        return 0;
    }

    // 添加一个查询，查询最近的站点

    auto query_connect_key = std::make_unique<std::string>(connect_key);
    if (redisAsyncCommand(_pub_context, Redis_Geo_Radius_Callback, query_connect_key.get(), "GEORADIUS " MPT_POSITION_LIST " %s %s 100 KM WITHDIST ASC", std::to_string(lon).c_str(), std::to_string(lat).c_str()) != REDIS_OK)
    {
        spdlog::warn("[{}:{}]: GEORADIUS command failed, mount [{}], user [{}], connect [{}]", __class__, __func__, channel, user_name, connect_key);
        return 1;
    }
    query_connect_key.release();

    return 0;
}

int caster_internal::sub_alias_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    // 先从别名映射表中查找实体基站列表

    auto alias_rule = _alias_rule_map.find(channel);
    if (alias_rule == _alias_rule_map.end())
    {
        caster_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = "Can't Find Alias Mount Point";
        cb(NULL, arg, &Reply);
        return 1;
    }

    for (auto &alias_mpt : alias_rule->second)
    {
        // 查找这个基站是否在线
        if (_active_mount_map.find(alias_mpt) == _active_mount_map.end())
        {
            continue;
        }
        else
        {
            // 判断这个挂载点和当前订阅的挂载点是同一个，那么就跳过
            if (channel == alias_mpt)
            {
                // 已经订阅了这个挂载点，跳过
                return 0;
            }

            // 如果不一致，要先把旧的订阅移除，然后添加到新的订阅上
            // 查询当前connect_key是否已经有订阅站点，
            auto sub_base_item = caster_internal::getInstance()->_base_sub_map.find(channel);
            if (sub_base_item != caster_internal::getInstance()->_base_sub_map.end()) // 没有这个订阅记录
            {
                auto sub_item = sub_base_item->second.find(connect_key);
                if (sub_item != sub_base_item->second.end())
                {
                    // 找到这个订阅记录，取消订阅
                    caster_internal::getInstance()->_base_sub_map[channel].erase(connect_key);
                }
            }

            // 更新用户订阅的挂载点信息
            auto item = caster_internal::getInstance()->_client_status_map.find(connect_key);
            if (item != caster_internal::getInstance()->_client_status_map.end())
            {
                item->second.set_alias_mpt(alias_mpt);
            }

            // 添加到新的订阅上去
            return caster_internal::getInstance()->sub_base_channel(alias_mpt.c_str(), user_name, connect_key, cb, arg, group_uid);
        }
    }

    caster_reply Reply;
    Reply.type = CasterReply::ERR;
    Reply.str = "Can't Find Useful Alias Mount Point"; // 实际使用的挂载点
    Reply.dval = 0.0;                                  // 距离
    cb(NULL, arg, &Reply);

    return 2;
}

int caster_internal::unsub_base_channel(const char *channel, const char *connect_key)
{
    if (_base_near_sub_map.find(connect_key) != _base_near_sub_map.end())
    {
        return unsub_near_channel(connect_key);
    }

    auto channel_subs = _base_sub_map.find(channel);
    if (channel_subs == _base_sub_map.end())
    {
        return 1;
    }

    auto item = channel_subs->second.find(connect_key);
    if (item == channel_subs->second.end())
    {
        return 1;
    }
    // 更新订阅者列表
    channel_subs->second.erase(item);

    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " MPT_SUBSCRIBE_LIST ":%s %s", channel, connect_key);

    return 0;
}

int caster_internal::unsub_near_channel(const char *connect_key)
{
    auto near_item = _base_near_sub_map.find(connect_key);
    if (near_item == _base_near_sub_map.end())
    {
        _near_pos_cache_map.erase(connect_key);
        return 0;
    }

    auto channel_subs = _base_sub_map.find(near_item->second.channel);
    if (channel_subs != _base_sub_map.end())
    {
        auto item = channel_subs->second.find(connect_key);
        if (item != channel_subs->second.end())
        {
            channel_subs->second.erase(item);
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " MPT_SUBSCRIBE_LIST ":%s %s", near_item->second.channel.c_str(), connect_key);
        }
    }

    _base_near_sub_map.erase(near_item);
    _near_pos_cache_map.erase(connect_key);
    return 0;
}

int caster_internal::sub_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    try
    {
        auto find = _rover_sub_map.find(channel);
        if (find == _rover_sub_map.end())
        {
            // 还没有订阅频道，添加订阅
            redisAsyncCommand(_sub_context, Redis_SUB_Rover_Callback, this, "SUBSCRIBE USR:%s", channel);
            std::unordered_map<std::string, caster_cb_item> channel_subs;
            _rover_sub_map.insert(std::pair<std::string, std::unordered_map<std::string, caster_cb_item>>(channel, channel_subs));

            // 由于该频道是此节点的第一次订阅，因此发送一次激活函数
            send_status_rover_channel(channel, "", CasterReply::ACTIVE, "First subscribe in one Caster Node");
        }
        find = _rover_sub_map.find(channel);

        // 更新订阅者列表
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_SUBSCRIBE_LIST ":%s EX %s FIELDS 1 %s %s",
                          channel,
                          _key_expire_time,
                          connect_key,
                          util_get_time_stamp_str().c_str());

        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.group_uid = normalize_access_group_uid(group_uid);
        cb_item.cb = cb;
        cb_item.arg = arg;
        find->second.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));

        caster_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = "";
        cb(NULL, arg, &Reply);
    }
    catch (const std::exception &e)
    {
        caster_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = e.what();
        cb(NULL, arg, &Reply);
    }
    return 0;
}

int caster_internal::unsub_rover_channel(const char *channel, const char *connect_key)
{
    auto channel_subs = _rover_sub_map.find(channel);
    if (channel_subs == _rover_sub_map.end())
    {
        return 1;
    }

    auto item = channel_subs->second.find(connect_key);
    if (item == channel_subs->second.end())
    {
        return 1;
    }
    // 更新订阅者列表
    channel_subs->second.erase(item);
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " USR_SUBSCRIBE_LIST ":%s %s", channel, connect_key);

    return 0;
}

int caster_internal::set_rover_coord_info(const char *user_name, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, int Q, int sat, double diff)
{
    // 更新坐标到状态信息中
    auto item = _client_status_map.find(connect_key);
    if (item == _client_status_map.end())
    {
        return 1;
    }
    item->second.set_coord_info(ecef_x, ecef_y, ecef_z, Q, sat, diff);
    // 将ECEF坐标转换成经纬度
    double lat = 0.0, lon = 0.0, alt = 0.0;
    util_ecef2pos(ecef_x, ecef_y, ecef_z, lat, lon, alt);
    // 更新坐标到GEO表中
    redisAsyncCommand(_pub_context, NULL, NULL, "GEOADD " USR_POSITION_LIST " %s %s %s", std::to_string(lon).c_str(), std::to_string(lat).c_str(), connect_key); //

    item->second.set_distance(0.0);
    const std::string &alias_mpt = item->second.alias_mpt();
    auto source = _source_decode_map.find(alias_mpt);
    if (source != _source_decode_map.end())
    {
        double base_ecef_x = 0.0;
        double base_ecef_y = 0.0;
        double base_ecef_z = 0.0;
        if (source->second.get_ecef_coord(base_ecef_x, base_ecef_y, base_ecef_z))
        {
            double dx = ecef_x - base_ecef_x;
            double dy = ecef_y - base_ecef_y;
            double dz = ecef_z - base_ecef_z;
            item->second.set_distance(std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }

    return 0;
}

int caster_internal::set_connect_delay_info(const char *connect_key, uint64_t delay)
{
    auto item = _stream_status_map.find(connect_key);
    if (item == _stream_status_map.end())
    {
        return 1;
    }
    item->second.add_delay(delay);
    return 0;
}

std::string caster_internal::get_source_list_text(const std::string &group_uid)
{
    navcaster::core::SourceTableService source_table(_source_record_map,
                                                     _source_decode_map,
                                                     _access_group_map,
                                                     _access_item_map,
                                                     _alias_visible_map);
    return source_table.build_text(group_uid);
}

int caster_internal::set_base_coord_info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z)
{
    // 更新坐标到状态信息中
    auto item = _server_status_map.find(connect_key);
    if (item == _server_status_map.end())
    {
        return 1;
    }
    item->second.set_coord_info(ecef_x, ecef_y, ecef_z);

    // 将ECEF坐标转换成经纬度
    double lat = 0.0, lon = 0.0, alt = 0.0;
    util_ecef2pos(ecef_x, ecef_y, ecef_z, lat, lon, alt);
    // 更新坐标到GEO表中
    redisAsyncCommand(_pub_context, NULL, NULL, "GEOADD " MPT_POSITION_LIST " %s %s %s", std::to_string(lon).c_str(), std::to_string(lat).c_str(), mount_point); //

    return 0;
}

int caster_internal::Set_Base_Source_Info(const char *mount_point, const char *connect_key, mount_info)
{
    return 0;
}

int caster_internal::set_base_source_info(const char *mount_point, const char *connect_key, const std::string &format_details, const std::string &nav_system)
{
    auto item = _server_status_map.find(connect_key);
    if (item == _server_status_map.end())
    {
        return 1;
    }
    item->second.set_source_info(format_details, nav_system);
    return 0;
}

int caster_internal::check_redis_connection()
{
    if (!_sub_context || !_is_sub_connected)
    {
        _is_sub_connected = false;
        subAttemptReconnect();
    }

    if (!_pub_context || !_is_pub_connected)
    {
        _is_pub_connected = false;
        pubAttemptReconnect();
    }

    if (!_sub_context || !_pub_context)
    {
        return 0;
    }

    if (_sub_ping_fail_count > 2)
    {
        spdlog::error("[{}] Redis SUB connection lost, try to reconnect...", __class__);

        _is_sub_connected = false;
        subAttemptReconnect();
        _sub_ping_fail_count = 0;
    }

    if (_pub_ping_fail_count > 2)
    {
        spdlog::error("[{}] Redis PUB connection lost, try to reconnect...", __class__);
        _is_pub_connected = false;
        pubAttemptReconnect();
        _pub_ping_fail_count = 0;
    }

    _sub_ping_fail_count++;
    _pub_ping_fail_count++;

    if (_sub_ping_fail_count == 1)
    {
        _sub_ping_time = std::chrono::high_resolution_clock::now();
    }
    if (_pub_ping_fail_count == 1)
    {
        _pub_ping_time = std::chrono::high_resolution_clock::now();
    }
    redisAsyncCommand(_pub_context, Redis_Pub_Ping_Callback, this, "PING");
    redisAsyncCommand(_sub_context, Redis_Sub_Ping_Callback, this, "PING");

    return 0;
}

int caster_internal::update_pull_base_info(const char *task_key, const char *alias_mpt, const char *connect_key, int state)
{
    auto stat_item = _server_status_map.find(connect_key);
    if (stat_item != _server_status_map.end())
    {
        stat_item->second.set_alias_mpt(alias_mpt);
    }

    auto pull_item = _pull_status_map.find(task_key);
    if (pull_item != _pull_status_map.end())
    {
        pull_item->second.update_state(connect_key, state);
        auto status_json = pull_item->second.toString();
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " PULL_STREAM_STATUS " EX %s FIELDS 1 %s %s",
                          std::to_string(_key_expire_time).c_str(), task_key, status_json.c_str());
    }
    return 0;
}

int caster_internal::update_push_rover_info(const char *task_key, const char *alias_mpt, const char *connect_key, int state)
{
    auto stat_item = _client_status_map.find(connect_key);
    if (stat_item != _client_status_map.end())
    {
        stat_item->second.set_alias_mpt(alias_mpt);
    }

    auto push_item = _push_status_map.find(task_key);
    if (push_item != _push_status_map.end())
    {
        push_item->second.update_state(connect_key, state);
        auto status_json = push_item->second.toString();
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " PUSH_STREAM_STATUS " EX %s FIELDS 1 %s %s",
                          std::to_string(_key_expire_time).c_str(), task_key, status_json.c_str());
    }
    return 0;
}

int caster_internal::upload_node_status()
{
    // 刷新一下速度
    add_sum_recv(0);
    add_sum_send(0);

    // 将本节点的信息上传到Redis
    caster_node node(_node_ID, _node_name, _startup_time);

    node.set_sys_usage();
    node.set_delay_info(_queue_delay, _sub_ping_delay, _sub_tcp_delay, _pub_ping_delay, _pub_tcp_delay);
    node.set_traffic_info(_send_total, _send_speed, _recv_total, _recv_speed);
    node.set_connection_count(_server_status_map.size(), _client_status_map.size());
    node.set_relay_count(count_running_relay_statuses(_pull_status_map, _node_ID),
                         count_running_relay_statuses(_push_status_map, _node_ID));
    node.set_extra_info(_hostname, static_cast<uint32_t>(_listen_port), static_cast<uint32_t>(_http_port),
                         static_cast<uint64_t>(_process_id), _http_port > 0);

    std::string node_json_text = node.toString();
    auto node_json = json::parse(node_json_text);

    redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " CASTER_NODE_INFO_LIST " EX %s FIELDS 1 %s %s",
                      std::to_string(_key_expire_time).c_str(),
                      _node_ID.c_str(),
                      node_json_text.c_str());

    // 节点历史快照 (每 NODE_HISTORY_INTERVAL 次调用记录一次)
    if (++_node_history_counter >= NODE_HISTORY_INTERVAL)
    {
        _node_history_counter = 0;
        record_node_history(node_json);
    }

    return 0;
}

void caster_internal::record_node_history(const json &node_json)
{
    auto writes = _node_history_recorder.record(node_json, util_get_now_second());
    for (const auto &write : writes)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "LPUSH %s %s", write.key.c_str(), write.value.c_str());
        redisAsyncCommand(_pub_context, NULL, NULL, "LTRIM %s 0 %d", write.key.c_str(), write.trim_max - 1);
    }
}

int caster_internal::try_set_master_node()
{
    redisAsyncCommand(_pub_context, NULL, NULL, "SET " CASTER_MASTER_KEY " %s NX EX %s", _node_ID.c_str(), std::to_string(_master_expire_time).c_str()); // 节点名   NODE为随机字符串+启动后从Redis中获取一个累加值
    redisAsyncCommand(_pub_context, Redis_SetMaster_Callback, this, "GET " CASTER_MASTER_KEY);
    return 0;
}

int caster_internal::sync_cluster_state()
{

    // 从云端获取所有的转发任务
    // STR:RELAY:LIST
    redisAsyncCommand(_pub_context, Redis_SyncPullList_Callback, this, "HGETALL " PULL_STREAM_RECORD);
    redisAsyncCommand(_pub_context, Redis_SyncPushList_Callback, this, "HGETALL " PUSH_STREAM_RECORD);
    // 从云端获取所有的任务状态
    // STR:RELAY:LIST
    redisAsyncCommand(_pub_context, Redis_SyncPullStat_Callback, this, "HGETALL " PULL_STREAM_STATUS);
    redisAsyncCommand(_pub_context, Redis_SyncPushStat_Callback, this, "HGETALL " PUSH_STREAM_STATUS);

    // 从云端获取所有节点的状态（这个放到最后一步，这个回调执行后要保证前面的数据都已经拿到）
    redisAsyncCommand(_pub_context, Redis_SyncClusterNode_Callback, this, "HGETALL " CASTER_NODE_INFO_LIST);

    return 0;
}

int caster_internal::relay_push_task_distribution()
{
    auto actions = navcaster::core::RelayScheduler::plan_push_distribution(_push_record_map,
                                                                           _push_status_map,
                                                                           _push_record_distributed);
    for (const auto &action : actions)
    {
        const auto payload = action.message.toString();
        redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH NODE:%s %s", _node_ID.c_str(), payload.c_str());
        navcaster::core::RelayScheduler::apply_distributed_mutation(action, _push_record_distributed);
    }

    return 0;
}

int caster_internal::relay_pull_task_distribution()
{
    auto actions = navcaster::core::RelayScheduler::plan_pull_distribution(_pull_record_map,
                                                                           _pull_status_map,
                                                                           _pull_record_distributed);
    for (const auto &action : actions)
    {
        const auto payload = action.message.toString();
        redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH NODE:%s %s", _node_ID.c_str(), payload.c_str());
        navcaster::core::RelayScheduler::apply_distributed_mutation(action, _pull_record_distributed);
    }

    return 0;
}

int caster_internal::relay_task_response(std::string req_str)
{
    // 根据接收到的广播，触发对应的回调函数，通知Caster外围创建和删除任务
    broadcast_msg req;
    if (req.fromString(req_str))
    {
        return 1; // 解析失败
    }

    auto uid = req.target;

    if (req.type == caster::core::BOARDCAST_TYPE_PULL_OPERATE)
    {
        if (req.operate == caster::core::BOARDCAST_OPERATR_ACTIVE)
        {
            if (_pull_status_map.count(uid))
            {
                return 2; // 已经存在这个任务，说明是重复的广播，忽略
            }

            pull_status stat(uid);
            stat.set_node_info(_node_ID, _node_name);
            stat.update_state("", 0);
            _pull_status_map.insert({uid, stat});
            _relay_cb(_relay_cb_arg, req);
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATR_INACTIVE)
        {
            if (_pull_status_map.find(uid) == _pull_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, req);
            _pull_status_map.erase(uid);
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " PULL_STREAM_STATUS " %s", uid.c_str());
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATE_UPDATE)
        {
            auto it = _pull_status_map.find(uid);
            if (it == _pull_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, req);
        }
    }
    else if (req.type == caster::core::BOARDCAST_TYPE_RUSH_OPERATE)
    {
        if (req.operate == caster::core::BOARDCAST_OPERATR_ACTIVE)
        {
            if (_push_status_map.count(uid))
            {
                return 2; // 已经存在这个任务，说明是重复的广播，忽略
            }

            push_status stat(uid);
            stat.set_node_info(_node_ID, _node_name);
            stat.update_state("", 0);
            _push_status_map.insert({uid, stat});
            _relay_cb(_relay_cb_arg, req);
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATR_INACTIVE)
        {
            if (_push_status_map.find(uid) == _push_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, req);
            _push_status_map.erase(uid);
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " PUSH_STREAM_STATUS " %s", uid.c_str());
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATE_UPDATE)
        {
            auto it = _push_status_map.find(uid);
            if (it == _push_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, req);
        }
    }

    return 0;
}

int caster_internal::upload_relay_status()
{
    for (auto iter = _pull_status_map.begin(); iter != _pull_status_map.end();)
    {
        if (iter->second.node_uid() != _node_ID)
        {
            ++iter;
            continue;
        }
        if (iter->second.running() &&
            _server_status_map.find(iter->second.connect_key()) == _server_status_map.end())
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " PULL_STREAM_STATUS " %s", iter->first.c_str());
            iter = _pull_status_map.erase(iter);
            continue;
        }
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " PULL_STREAM_STATUS " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter->first.c_str(), iter->second.toString().c_str());
        ++iter;
    }
    for (auto iter = _push_status_map.begin(); iter != _push_status_map.end();)
    {
        if (iter->second.node_uid() != _node_ID)
        {
            ++iter;
            continue;
        }
        if (iter->second.running() &&
            _client_status_map.find(iter->second.connect_key()) == _client_status_map.end())
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " PUSH_STREAM_STATUS " %s", iter->first.c_str());
            iter = _push_status_map.erase(iter);
            continue;
        }
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " PUSH_STREAM_STATUS " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter->first.c_str(), iter->second.toString().c_str());
        ++iter;
    }
    return 0;
}

int caster_internal::update_alias_source()
{
    // 根据当前已经在线的挂载点（决定源列表中是否有此挂载点信息，同时把实际源的挂载点信息同步到Redis）

    return 0;
}

void caster_internal::Redis_SetMaster_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    // 读取当前 master 节点 ID
    if (reply && reply->type == REDIS_REPLY_STRING && reply->str)
    {
        const auto observed = navcaster::core::MasterLeaseService::observe_master(svr->_current_master_id,
                                                                                  reply->str,
                                                                                  svr->_node_ID);
        if (observed.changed)
        {
            svr->_current_master_id = observed.current_master_id;
            if (observed.is_self)
            {
                spdlog::info("[caster_internal]: This node ({}) became MASTER", svr->_node_ID);
            }
            else
            {
                spdlog::info("[caster_internal]: Master node changed to {}", observed.current_master_id);
            }
        }
    }

    // 如果返回的节点名和自己的节点名是一致的，那么给这个节点续期
    redisAsyncCommand(svr->_pub_context, Redis_KeepMaster_Callback, svr, "SET " CASTER_MASTER_KEY " %s IFEQ %s EX %s",
                      svr->_node_ID.c_str(),
                      svr->_node_ID.c_str(),
                      std::to_string(svr->_master_expire_time).c_str());

    // 如果自己已经不是主节点，那么要清理本地维护的主节点状态信息
}

void caster_internal::Redis_KeepMaster_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    // 检查续期是否成功 (reply 为 OK 表示成功, nil 表示已非 master)
    bool renewed = (reply && reply->type == REDIS_REPLY_STATUS && reply->str && std::string(reply->str) == "OK");

    const auto plan = navcaster::core::MasterLeaseService::apply_keepalive_result(svr->_is_master,
                                                                                  renewed,
                                                                                  svr->_node_ID,
                                                                                  util_get_now_second());

    if (plan.event == navcaster::core::MasterLeaseEventType::Acquired)
    {
        svr->_is_master = plan.is_master;
        spdlog::info("[caster_internal]: Node {} confirmed as MASTER (TTL={}s)", svr->_node_ID, svr->_master_expire_time);
        redisAsyncCommand(svr->_pub_context, NULL, NULL, "HSET %s %s %s", plan.log_key.c_str(), plan.log_field.c_str(), plan.payload.dump().c_str());
    }
    else if (plan.event == navcaster::core::MasterLeaseEventType::Lost)
    {
        svr->_is_master = plan.is_master;
        spdlog::warn("[caster_internal]: Node {} lost MASTER role", svr->_node_ID);
        redisAsyncCommand(svr->_pub_context, NULL, NULL, "HSET %s %s %s", plan.log_key.c_str(), plan.log_field.c_str(), plan.payload.dump().c_str());
    }

    if (plan.trigger_cluster_sync)
    {
        // Master 节点续期成功，开始执行节点任务
        svr->sync_cluster_state();
    }
}

void caster_internal::Redis_NodeChannel_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 接收广播信息，触发回调执行任务
    // 订阅到的是一个Json字符串

    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }
    if (reply->elements != 3)
    {
        return; // 异常的回调参数
    }

    auto re1 = reply->element[0];
    auto re2 = reply->element[1];
    auto re3 = reply->element[2];

    if (re3->type != REDIS_REPLY_STRING)
    {
        return; // 回复不是字符串，第一次订阅这个频道的时候回应为 REDIS_REPLY_INTEGER
    }

    svr->relay_task_response(re3->str);
}

void caster_internal::Redis_SyncClusterNode_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 将节点的状态更新到本地（这个应该已经是最后一个主节点同步函数），这个函数执行完之后，开始执行主节点的分发任务
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL CASTER:NODE reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL CASTER:NODE reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    svr->_cluster_node_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        svr->_cluster_node_map.insert(std::pair<std::string, std::string>(field, value));
    }

    svr->relay_pull_task_distribution();
    svr->relay_push_task_distribution();
}

void caster_internal::Redis_SyncPullList_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 将任务列表更新到本地
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL PULL:RECORD reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL PULL:RECORD reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    svr->_pull_record_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        // json转换成relay_item
        pull_record item(field);
        if (item.fromString(value))
        {
            // 解析失败
            continue;
        }
        svr->_pull_record_map.insert(std::pair<std::string, pull_record>(field, item));
    }
}

void caster_internal::Redis_SyncPullStat_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 将任务状态更新到本地

    // 筛选需要关闭，启动的任务，进行任务分发

    // 将任务列表更新到本地
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL PULL:STAT reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL PULL:STAT reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    svr->_pull_status_map.clear();
    size_t running_count = 0;

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        // json转换成relay_item
        pull_status item(field);
        if (item.fromString(value))
        {
            // 解析失败
            continue;
        }
        if (item.running())
        {
            if (item.node_uid() == svr->_node_ID &&
                svr->_server_status_map.find(item.connect_key()) == svr->_server_status_map.end())
            {
                redisAsyncCommand(svr->_pub_context, NULL, NULL, "HDEL " PULL_STREAM_STATUS " %s", field);
                continue;
            }
            running_count++;
        }
        svr->_pull_status_map.insert(std::pair<std::string, pull_status>(field, item));
    }
    svr->_pull_connection_count = running_count;
}

void caster_internal::Redis_SyncPushList_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 将任务列表更新到本地
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL PUSH:RECORD reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL PUSH:RECORD reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    svr->_push_record_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        // json转换成relay_item
        push_record item(field);
        if (item.fromString(value))
        {
            // 解析失败
            continue;
        }
        svr->_push_record_map.insert(std::pair<std::string, push_record>(field, item));
    }
}

void caster_internal::Redis_SyncPushStat_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 将任务状态更新到本地

    // 筛选需要关闭，启动的任务，进行任务分发

    // 将任务列表更新到本地
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL PUSH:STAT reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL PUSH:STAT reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    svr->_push_status_map.clear();
    size_t running_count = 0;

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        // json转换成relay_item
        push_status item(field);
        if (item.fromString(value))
        {
            // 解析失败
            continue;
        }
        if (item.running())
        {
            if (item.node_uid() == svr->_node_ID &&
                svr->_client_status_map.find(item.connect_key()) == svr->_client_status_map.end())
            {
                redisAsyncCommand(svr->_pub_context, NULL, NULL, "HDEL " PUSH_STREAM_STATUS " %s", field);
                continue;
            }
            running_count++;
        }
        svr->_push_status_map.insert(std::pair<std::string, push_status>(field, item));
    }
    svr->_push_connection_count = running_count;
}

int caster_internal::clear_overdue_item()
{
    // 当初设计的时候，_base_register_map其实就完全对应了Redis中的MPT:LIST中的内容
    // MPT:LIST也被用作生成在线挂载点列表(SOURCE TABLE),因此考虑到如果这个挂载点实际上没有实际的连接存在
    // 即：虽然这个挂载点历史存在，但是当前已经没有承担推送该挂载点的实际连接，因此认为这个挂载点已经离线了
    // 在单节点模式中，则应当更新并删除MPT:LIST中已经没有数据的条目
    // 但考虑到多节点模式，则该点不应该立即删除，因为其他挂载点可能也会维护它，因此改为采用超时机制来清理MPT:LIST中不存在的挂载点更为合理

    auto register_base = _base_register_map; // 创建一个副本
    for (auto iter : register_base)          // 迭代副本
    {
        if (iter.second.size() == 0)
        {
            _base_register_map.erase(iter.first);
        }
    }

    auto register_rover = _rover_register_map; // 创建一个副本
    for (auto iter : register_rover)           // 迭代副本
    {
        if (iter.second.size() == 0)
        {
            _rover_register_map.erase(iter.first);
            // redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " USR_ONLINE_LIST " %s", iter.first.c_str());
        }
    }

    return 0;
}

int caster_internal::test_queue_delay()
{

    // 不需要考虑这个激活时间比较晚的情况，因为这个函数是在定时回调中执行激活的，下次再调用这个函数一定排在_testdelay_ev之后

    // 记录激活时间
    _activate_time = std::chrono::high_resolution_clock::now();
    // 测试消息队列的延迟情况
    event_active(_testdelay_ev, 0, 1);

    return 0;
}

int caster_internal::upload_record_item()
{
    // 数据流状态   ConnectKey/数据流状态
    for (auto &str : _stream_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " STR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.toString().c_str());
    }

    // 基站状态   ConnectKey/基站状态
    for (auto &str : _server_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.toString().c_str());
        if (!str.second._login_mpt.empty())
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " SOURCE_DECODE_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.second._login_mpt.c_str(), str.second.toSource().c_str());
        }
    }

    // 用户状态   ConnectKey/用户状态
    for (auto &str : _client_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.toString().c_str());
    }

    for (auto iter : _base_register_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_ONLINE_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter.first.c_str(), util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间
        for (auto items : iter.second)
        {
            // 更新基站注册连接有效期
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE " MPT_CONNECTION_LIST ":%s %s FIELDS 1 %s", items.second.channel.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    for (auto iter : _base_sub_map)
    {
        for (auto items : iter.second)
        {
            // 更新本地订阅基站频道的连接信息
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE " MPT_SUBSCRIBE_LIST ":%s %s FIELDS 1 %s", items.second.channel.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    for (auto iter : _rover_register_map)
    {
        // 更新本地维护的用户
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_ONLINE_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter.first.c_str(), util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        for (auto items : iter.second)
        {
            // 更新用户注册连接
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE " USR_CONNECTION_LIST ":%s %s FIELDS 1 %s", items.second.user_name.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个用户连接续期
        }
    }

    for (auto iter : _rover_sub_map)
    {
        for (auto items : iter.second)
        {
            // 更新本地订阅用户频道的连接信息
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE " USR_SUBSCRIBE_LIST ":%s %s FIELDS 1 %s", items.second.channel.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    // 刷新连接历史记录的 last_update 以及收发统计
    {
        auto now = util_get_now_second();
        for (auto &[ck, entry] : _base_history_map)
        {
            entry["last_update"] = now;
            auto stt = _stream_status_map.find(ck);
            if (stt != _stream_status_map.end())
            {
                entry["send_total"] = stt->second.getSendTotal();
                entry["recv_total"] = stt->second.getRecvTotal();
            }
            std::string mount = entry.value("name", std::string());
            std::string field = std::to_string(entry.value("connect_time", 0LL)) + "_" + ck;
            std::string key = std::string(LOG_MPT_PREFIX) + mount;
            redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", key.c_str(), field.c_str(), entry.dump().c_str());
        }
        for (auto &[ck, entry] : _rover_history_map)
        {
            entry["last_update"] = now;
            auto stt = _stream_status_map.find(ck);
            if (stt != _stream_status_map.end())
            {
                entry["send_total"] = stt->second.getSendTotal();
                entry["recv_total"] = stt->second.getRecvTotal();
            }
            std::string user = entry.value("name", std::string());
            std::string field = std::to_string(entry.value("connect_time", 0LL)) + "_" + ck;
            std::string key = std::string(LOG_USR_PREFIX) + user;
            redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", key.c_str(), field.c_str(), entry.dump().c_str());
        }
    }

    return 0;
}

int caster_internal::download_active_item()
{
    redisAsyncCommand(_pub_context, Redis_Update_Active_Base_Callback, this, "HGETALL " MPT_ONLINE_LIST);
    redisAsyncCommand(_pub_context, Redis_Update_Active_Rover_Callback, this, "HGETALL " USR_ONLINE_LIST);
    redisAsyncCommand(_pub_context, Redis_Update_Decode_Source_Callback, this, "HGETALL " SOURCE_DECODE_LIST);
    redisAsyncCommand(_pub_context, Redis_Update_Record_Source_Callback, this, "HGETALL " SOURCE_RECORD_LIST);

    redisAsyncCommand(_pub_context, Redis_Get_Hash_Lenth_Callback, &_server_connection_count, "HLEN " MPT_STATUS_LIST);
    redisAsyncCommand(_pub_context, Redis_Get_Hash_Lenth_Callback, &_client_connection_count, "HLEN " USR_STATUS_LIST);
    redisAsyncCommand(_pub_context, Redis_SyncPullStat_Callback, this, "HGETALL " PULL_STREAM_STATUS);
    redisAsyncCommand(_pub_context, Redis_SyncPushStat_Callback, this, "HGETALL " PUSH_STREAM_STATUS);
    return 0;
}

int caster_internal::download_alias_rule()
{
    redisAsyncCommand(_pub_context, Redis_Update_Alias_Rule_Callback, this, "HGETALL " ALIAS_RULE_LIST);

    return 0;
}

int caster_internal::download_access_policy()
{
    redisAsyncCommand(_pub_context, Redis_Update_Access_Group_Callback, this, "HGETALL " ACCESS_GROUP);

    return 0;
}

int caster_internal::check_active_base_channel()
{
    auto sub_map = _base_sub_map; // 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错
    for (auto channel_subs = sub_map.begin(); channel_subs != sub_map.end(); channel_subs++)
    // for (auto channel_subs : _sub_cb_map)
    {
        if (_active_mount_map.find(channel_subs->first) == _active_mount_map.end() && _notify_base_inactive) // 该订阅频道不在活跃频道中
        {
            for (auto item = channel_subs->second.begin(); item != channel_subs->second.end(); item++) // 关闭所有订阅者
            {
                caster_reply Reply;
                Reply.type = CasterReply::ERR;
                Reply.str = "Subscribe Base is not active";
                auto cb_arg = item->second;
                cb_arg.cb(channel_subs->first.c_str(), cb_arg.arg, &Reply);
            }
        }

        if (channel_subs->second.size() == 0) // 订阅频道的实际用户为0
        {
            _base_sub_map.erase(channel_subs->first);                                                                                      // 实际执行的操作是删除了原始记录
            redisAsyncCommand(_sub_context, NULL, NULL, "UNSUBSCRIBE MPT:%s", channel_subs->first.c_str());                                // 取消订阅该基站频道
            send_status_base_channel(channel_subs->first.c_str(), "", CasterReply::INACTIVE, "one Caster Node unsubscribe this channel "); // 由该节点已经不再订阅，发送一次取消激活函数
        }
    }

    return 0;
}

int caster_internal::check_active_rover_channel()
{
    auto sub_map = _rover_sub_map; // 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错
    for (auto channel_subs = sub_map.begin(); channel_subs != sub_map.end(); channel_subs++)
    // for (auto channel_subs : _sub_cb_map) //不能用这个格式，推导的格式不正确？
    {
        if (_active_user_map.find(channel_subs->first) == _active_user_map.end() && _notify_rover_inactive) // 该订阅频道不在活跃频道中
        {
            for (auto item = channel_subs->second.begin(); item != channel_subs->second.end(); item++)
            {
                caster_reply Reply;
                Reply.type = CasterReply::ERR;
                Reply.str = "Subscribe Rover is not active";
                auto cb_arg = item->second;
                cb_arg.cb(channel_subs->first.c_str(), cb_arg.arg, &Reply);
            }
        }
        if (channel_subs->second.size() == 0) // 订阅频道的实际用户为0
        {
            _rover_sub_map.erase(channel_subs->first);                                                                                      // 实际执行的操作是删除了原始记录
            redisAsyncCommand(_sub_context, NULL, NULL, "UNSUBSCRIBE USR:%s", channel_subs->first.c_str());                                 // 取消订阅该基站频道
            send_status_rover_channel(channel_subs->first.c_str(), "", CasterReply::INACTIVE, "one Caster Node unsubscribe this channel "); // 由该节点已经不再订阅，发送一次取消激活函数
        }
    }

    return 0;
}

// int caster_internal::build_source_list()
// {
//     std::string items;
//     for (auto iter : _active_base_info_map)
//     {
//         mount_info item;
//         auto info = _mount_map.find(iter);
//         if (info == _mount_map.end())
//         {
//             item = build_default_mount_info(iter);
//         }
//         else
//         {
//             item = info->second;
//         }
//         items += convert_mount_info_to_string(item);
//     }

//     _source_list_text = items;
//     return 0;
// }



void caster_internal::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<caster_internal *>(arg);

    // Redis 失联时保留事件循环和 HTTP health，不执行依赖 Redis 的周期任务。
    if (!svr->_pub_context || !svr->_sub_context || !svr->_is_pub_connected || !svr->_is_sub_connected)
    {
        svr->check_redis_connection();
        return;
    }

    svr->test_queue_delay();

    svr->upload_node_status(); // 上传当前节点的状态   上传到CASTER:NODE中添加一条记录

    svr->upload_relay_status(); // 上传当前节点的转发任务状态 到 PULL:STAT 和 PUSH:STAT 中

    svr->try_set_master_node(); // 尝试设置为主节点

    // 判断ping时间是否已经超过过期时间
    // 如果过期，认为连接已出现未知状况，连接状态置为0，触发重连机制

    // 向redis ping，根据回调确认连接正常
    svr->check_redis_connection();

    //  清除本地不再有实际连接注册的基站，这样就不会给这些已经不在线的基站在MPT:LIST中续期
    svr->clear_overdue_item();

    // 本地维护的在线挂载点续期
    // 维护所有的在线挂载点和用户
    // 维护所有的在线记录
    svr->upload_record_item();

    // 获取所有在线挂载点、在线用户列表，删除没有按时续期的用户和挂载点
    svr->download_active_item();

    // 获取别名规则列表
    svr->download_alias_rule();
    // 4.下载访问控制策略
    svr->download_access_policy();
}

void caster_internal::TestDelayCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<caster_internal *>(arg);
    svr->_execute_time = std::chrono::high_resolution_clock::now();
    svr->_queue_delay = std::chrono::duration_cast<std::chrono::microseconds>(svr->_execute_time - svr->_activate_time).count();
}

int caster_internal::relay_register_callback(RelayCallback cb, void *arg)
{
    _relay_cb_arg = arg;
    _relay_cb = cb;
    return 0;
}

int caster_internal::register_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type, const char *group_uid)
{
    std::string deny_reason;
    if (!check_mount_access(normalize_access_group_uid(group_uid), channel, &deny_reason))
    {
        caster_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = deny_reason.c_str();
        cb(NULL, arg, &Reply);
        return 1;
    }

    // 查询要注册的频道
    auto find = _base_register_map.find(channel);
    if (find == _base_register_map.end())
    {
        // 还没有该频道的注册记录
        std::unordered_map<std::string, caster_cb_item> channel_cbs;
        _base_register_map.insert(std::pair<std::string, std::unordered_map<std::string, caster_cb_item>>(channel, channel_cbs));
    }
    find = _base_register_map.find(channel);

    try
    {
        // 创建一条新的连接记录
        server_status conn(connect_key);
        conn.set_info(channel, static_cast<int>(type), user_name);

        _server_status_map.insert(std::pair<std::string, server_status>(connect_key, conn));

        // 数据流记录
        stream_status str(connect_key);

        _stream_status_map.insert(std::pair<std::string, stream_status>(connect_key, str));

        // 创建RTCM解码器，用于解析数据流中的坐标和报文统计
        _base_decoder_map.emplace(connect_key, decode_rtcm{});

        // // 向云端插入记录

        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, conn.toString().c_str());
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " STR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, str.toString().c_str());

        // 写入连接历史记录 (持久化, 不设过期)
        {
            long long now_ts = util_get_now_second();
            json log_entry;
            log_entry["name"] = channel;
            log_entry["connect_key"] = connect_key;
            log_entry["node_id"] = _node_ID;
            log_entry["type"] = static_cast<int>(type);
            log_entry["account"] = user_name;
            log_entry["host"] = conn._ip;
            log_entry["port"] = conn._port;
            log_entry["connect_time"] = now_ts;
            log_entry["last_update"] = now_ts;
            log_entry["disconnect_time"] = 0;
            log_entry["send_total"] = 0;
            log_entry["recv_total"] = 0;
            std::string log_key = std::string(LOG_MPT_PREFIX) + channel;
            std::string log_field = std::to_string(now_ts) + "_" + connect_key;
            _base_history_map[connect_key] = log_entry;
            redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", log_key.c_str(), log_field.c_str(), log_entry.dump().c_str());
        }

        // 将cb注册回调记录到本地
        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.group_uid = normalize_access_group_uid(group_uid);
        cb_item.cb = cb;
        cb_item.arg = arg;

        if (find->second.find(connect_key) != find->second.end())
        {
            throw std::invalid_argument("Connect_Key is already in the register map");
        }
        find->second.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性
        // 即：查询到的结果已经包含当前记录，因此避免查询-插入后还需要再进行一步检测的步骤
        // 问题：如果两个节点同时插入了记录，同时查询到记录，按照规则，可能都会被踢掉？
        //       但是踢掉也只是通过广播的形式来踢掉，没啥影响，大不了都登不上，都下线一次
        // 反正记录只能由注册者自己删除（或者说注册该连接的Caster维护）

        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_CONNECTION_LIST ":%s EX %s FIELDS 1 %s %s",
                          channel,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理

        auto ctx = new std::pair<caster_internal *, caster_cb_item>(this, cb_item);
        redisAsyncCommand(_pub_context, Redis_Register_Base_Callback, ctx, "HGETALL " MPT_CONNECTION_LIST ":%s", channel); // 查询当前频道的所有记录
    }
    catch (std::exception &e)
    {
        // 发生异常，此次插入失败
        spdlog::warn("[{}:{}]: exception:  {}", __class__, __func__, e.what());
        return 1;
    }

    return 0;
}

int caster_internal::register_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type, const char *group_uid)
{
    std::string deny_reason;
    bool allowed = type == CasterRegisterType::NEAREST
                       ? check_nearest_mount_login(normalize_access_group_uid(group_uid), channel, &deny_reason)
                       : check_mount_access(normalize_access_group_uid(group_uid), channel, &deny_reason);
    if (!allowed)
    {
        caster_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = deny_reason.c_str();
        cb(NULL, arg, &Reply);
        return 1;
    }

    // 查询要注册的频道
    auto find = _rover_register_map.find(user_name);
    if (find == _rover_register_map.end())
    {
        // 还没有该频道的注册记录
        std::unordered_map<std::string, caster_cb_item> channel_cbs;
        _rover_register_map.insert(std::pair<std::string, std::unordered_map<std::string, caster_cb_item>>(user_name, channel_cbs));
    }
    find = _rover_register_map.find(user_name);

    try
    {
        // 创建一条新的连接记录
        client_status conn(connect_key);
        conn.set_info(channel, static_cast<int>(type), user_name);

        _client_status_map.insert(std::pair<std::string, client_status>(connect_key, conn));

        // 数据流记录
        stream_status str(connect_key);

        _stream_status_map.insert(std::pair<std::string, stream_status>(connect_key, str));

        // 创建NMEA解码器
        _rover_decoder_map.emplace(connect_key, decode_nmea{});
        

        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, conn.toString().c_str()); // 更新挂载点数据生产者的更新时间
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " STR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, str.toString().c_str());

        // 写入连接历史记录 (持久化, 不设过期)
        {
            long long now_ts = util_get_now_second();
            json log_entry;
            log_entry["name"] = user_name;
            log_entry["mount"] = channel;
            log_entry["connect_key"] = connect_key;
            log_entry["node_id"] = _node_ID;
            log_entry["type"] = static_cast<int>(type);
            log_entry["account"] = user_name;
            std::string _h, _s; int _hp, _sp;
            decodeKey(connect_key, _s, _sp, _h, _hp);
            log_entry["host"] = _h;
            log_entry["port"] = _hp;
            log_entry["connect_time"] = now_ts;
            log_entry["last_update"] = now_ts;
            log_entry["disconnect_time"] = 0;
            log_entry["send_total"] = 0;
            log_entry["recv_total"] = 0;
            std::string log_key = std::string(LOG_USR_PREFIX) + user_name;
            std::string log_field = std::to_string(now_ts) + "_" + connect_key;
            _rover_history_map[connect_key] = log_entry;
            redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", log_key.c_str(), log_field.c_str(), log_entry.dump().c_str());
        }

        // 将cb注册回调记录到本地
        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.group_uid = normalize_access_group_uid(group_uid);
        cb_item.cb = cb;
        cb_item.arg = arg;

        if (find->second.find(connect_key) != find->second.end())
        {
            throw std::invalid_argument("Connect_Key is already in the register map");
        }
        find->second.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));
        _kick_map[connect_key] = cb_item;

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性，即：查询到的结果已经包含当前记录，因此避免查询-插入-再查询的时候
        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_CONNECTION_LIST ":%s EX %s FIELDS 1 %s %s",
                          user_name,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理
        auto ctx = new std::pair<caster_internal *, caster_cb_item>(this, cb_item);
        redisAsyncCommand(_pub_context, Redis_Register_Rover_Callback, ctx, "HGETALL " USR_CONNECTION_LIST ":%s", user_name); // 查询当前频道的所有记录
    }
    catch (std::exception &e)
    {
        // 发生异常，此次插入失败
        spdlog::warn("[{}:{}]: exception:  {}", __class__, __func__, e.what());
        return 1;
    }

    return 0;
}

int caster_internal::withdraw_base_channel(const char *channel, const char *user_name, const char *connect_key)
{
    // 从该频道的HASH Map中删除指定Connect_Key记录
    // 删除成功
    // 从本地注册回调Map中删除指定的记录

    // 查询要注册的频道
    auto channel_registers = _base_register_map.find(channel);
    if (channel_registers == _base_register_map.end())
    {
        // 错误：没有该频道的注册记录
        return 1;
    }
    auto item = channel_registers->second.find(connect_key);
    if (item == channel_registers->second.end())
    {
        // 错误：没有该连接的注册记录
        return 2;
    }

    // 删除该条记录
    channel_registers->second.erase(item);
    _kick_map.erase(connect_key);
    // 删除Redis记录
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " MPT_CONNECTION_LIST ":%s %s", channel, connect_key);

    // 删除status记录
    auto str = _server_status_map.find(connect_key);
    if (str == _server_status_map.end())
    {
        // 错误，找不到这条stream状态记录仪
        return 3;
    }
    _server_status_map.erase(connect_key);
    // Capture stream totals before erasing
    double log_send_total = 0, log_recv_total = 0;
    {
        auto stt = _stream_status_map.find(connect_key);
        if (stt != _stream_status_map.end())
        {
            log_send_total = stt->second.getSendTotal();
            log_recv_total = stt->second.getRecvTotal();
        }
    }
    _stream_status_map.erase(connect_key);
    _base_decoder_map.erase(connect_key);
    // // 向云端插入记录
    if (_upload_base_stat)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " MPT_STATUS_LIST " %s ", connect_key);
    }

    // 更新连接历史记录 (写入断开时间)
    {
        auto hit = _base_history_map.find(connect_key);
        if (hit != _base_history_map.end())
        {
            long long now_ts = util_get_now_second();
            hit->second["disconnect_time"] = now_ts;
            hit->second["last_update"] = now_ts;
            hit->second["send_total"] = log_send_total;
            hit->second["recv_total"] = log_recv_total;
            std::string log_key = std::string(LOG_MPT_PREFIX) + channel;
            std::string log_field = std::to_string(hit->second.value("connect_time", now_ts)) + "_" + connect_key;
            redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", log_key.c_str(), log_field.c_str(), hit->second.dump().c_str());
            _base_history_map.erase(hit);
        }
    }

    return 0;
}

int caster_internal::withdraw_rover_channel(const char *channel, const char *user_name, const char *connect_key)
{
    // 查询要注册的频道
    auto channel_registers = _rover_register_map.find(user_name);
    if (channel_registers == _rover_register_map.end())
    {
        // 错误：没有该频道的注册记录
        return 1;
    }
    auto item = channel_registers->second.find(connect_key);
    if (item == channel_registers->second.end())
    {
        // 错误：没有该连接的注册记录
        return 2;
    }

    // 删除该条记录
    channel_registers->second.erase(item);
    _kick_map.erase(connect_key);
    // 删除Redis记录
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " USR_CONNECTION_LIST ":%s %s", user_name, connect_key);

    // 删除status记录
    auto str = _client_status_map.find(connect_key);
    if (str == _client_status_map.end())
    {
        // 错误，找不到这条stream状态记录仪
        return 3;
    }
    _client_status_map.erase(connect_key);
    // Capture stream totals before erasing
    double log_send_total = 0, log_recv_total = 0;
    {
        auto stt = _stream_status_map.find(connect_key);
        if (stt != _stream_status_map.end())
        {
            log_send_total = stt->second.getSendTotal();
            log_recv_total = stt->second.getRecvTotal();
        }
    }
    _stream_status_map.erase(connect_key);
    _rover_decoder_map.erase(connect_key);
    unsub_near_channel(connect_key);
    // 向云端插入记录
    if (_upload_rover_stat)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " USR_STATUS_LIST " %s", connect_key);
    }

    // 更新连接历史记录 (写入断开时间)
    {
        auto hit = _rover_history_map.find(connect_key);
        if (hit != _rover_history_map.end())
        {
            long long now_ts = util_get_now_second();
            hit->second["disconnect_time"] = now_ts;
            hit->second["last_update"] = now_ts;
            hit->second["send_total"] = log_send_total;
            hit->second["recv_total"] = log_recv_total;
            std::string log_key = std::string(LOG_USR_PREFIX) + user_name;
            std::string log_field = std::to_string(hit->second.value("connect_time", now_ts)) + "_" + connect_key;
            redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s", log_key.c_str(), log_field.c_str(), hit->second.dump().c_str());
            _rover_history_map.erase(hit);
        }
    }

    return 0;
}

int caster_internal::send_status_base_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason)
{
    if (auto result = require_publish_context(_pub_context, __func__, "CASTER:BROADCAST", safe_cstr(connect_key)); !result.ok())
    {
        return finish_redis_publish_failure(result);
    }

    // 向redis发布广播
    broadcast_msg item;
    item.type = caster::core::BOARDCAST_TYPE_SERVER_OPERATE;
    item.operate = broadcast_msg::ReplyToOperate(status);
    item.target = safe_cstr(connect_key);
    item.msg_str = safe_cstr(channel); // 状态变更时msg_str填充channel
    item.reason_str = safe_cstr(reason);

    int ret = redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH CASTER:BROADCAST %s", item.toString().c_str());
    if (ret != REDIS_OK)
    {
        return finish_redis_publish_failure(CoreResult::failure(CoreErrorCode::PublishFailed,
                                                                __func__,
                                                                "Redis publish command failed")
                                                .with_redis_key("CASTER:BROADCAST")
                                                .with_subject(safe_cstr(connect_key)));
    }
    return REDIS_OK;
}

int caster_internal::pub_base_channel(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    const std::string redis_key = std::string("MPT:") + safe_cstr(mount_point);
    if (auto result = require_publish_context(_pub_context, __func__, redis_key, safe_cstr(connect_key)); !result.ok())
    {
        return finish_redis_publish_failure(result);
    }
    if (data_length > 0 && data == nullptr)
    {
        return finish_redis_publish_failure(CoreResult::failure(CoreErrorCode::InvalidArgument,
                                                                __func__,
                                                                "missing required argument")
                                                .with_subject("data")
                                                .with_redis_key(redis_key));
    }

    auto str = _stream_status_map.find(connect_key);
    if (str != _stream_status_map.end())
    {
        str->second.add_recv(data_length);
        add_sum_recv(data_length);
    }

    // 解析RTCM数据流，提取坐标和报文统计信息
    auto dec = _base_decoder_map.find(connect_key);
    if (dec != _base_decoder_map.end())
    {
        dec->second.Decode(data, data_length);

        // 更新坐标信息
        if (dec->second._has_position)
        {
            set_base_coord_info(mount_point, connect_key,
                                dec->second._ecef_x, dec->second._ecef_y, dec->second._ecef_z);
        }

        // 更新源列表解析信息（报文类型、卫星系统）
        if (!dec->second._msg_stats.empty())
        {
            set_base_source_info(mount_point, connect_key,
                                 dec->second.get_format_details(), dec->second.get_nav_system());
        }
    }

    int ret = redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH MPT:%s %b", safe_cstr(mount_point), data, data_length);
    if (ret != REDIS_OK)
    {
        return finish_redis_publish_failure(CoreResult::failure(CoreErrorCode::PublishFailed,
                                                                __func__,
                                                                "Redis publish command failed")
                                                .with_redis_key(redis_key)
                                                .with_subject(safe_cstr(connect_key)));
    }
    return REDIS_OK;
}

int caster_internal::send_status_rover_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason)
{
    if (auto result = require_publish_context(_pub_context, __func__, "CASTER:BROADCAST", safe_cstr(connect_key)); !result.ok())
    {
        return finish_redis_publish_failure(result);
    }

    // 向redis发布广播
    broadcast_msg item;
    item.type = caster::core::BOARDCAST_TYPE_CLIENT_OPERATE;
    item.operate = broadcast_msg::ReplyToOperate(status);
    item.target = safe_cstr(connect_key);
    item.msg_str = safe_cstr(channel); // 状态变更时msg_str填充channel
    item.reason_str = safe_cstr(reason);

    int ret = redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH CASTER:BROADCAST %s", item.toString().c_str());
    if (ret != REDIS_OK)
    {
        return finish_redis_publish_failure(CoreResult::failure(CoreErrorCode::PublishFailed,
                                                                __func__,
                                                                "Redis publish command failed")
                                                .with_redis_key("CASTER:BROADCAST")
                                                .with_subject(safe_cstr(connect_key)));
    }
    return REDIS_OK;
}

int caster_internal::pub_rover_channel(const char *user_name, const char *connect_key, const char *data, size_t data_length)
{
    const std::string redis_key = std::string("USR:") + safe_cstr(user_name);
    if (auto result = require_publish_context(_pub_context, __func__, redis_key, safe_cstr(connect_key)); !result.ok())
    {
        return finish_redis_publish_failure(result);
    }
    if (data_length > 0 && data == nullptr)
    {
        return finish_redis_publish_failure(CoreResult::failure(CoreErrorCode::InvalidArgument,
                                                                __func__,
                                                                "missing required argument")
                                                .with_subject("data")
                                                .with_redis_key(redis_key));
    }

    auto str = _stream_status_map.find(connect_key);
    if (str != _stream_status_map.end())
    {
        str->second.add_recv(data_length);
        add_sum_recv(data_length);
    }

    // 解析NMEA/GGA数据并更新用户坐标（所有订阅类型均执行）
    auto dec = _rover_decoder_map.find(connect_key);
    if (dec != _rover_decoder_map.end())
    {
        dec->second.Decode(data, data_length);
        if (dec->second._has_position)
        {
            // 更新用户坐标
            set_rover_coord_info(user_name, connect_key,
                                 dec->second._ecef_x, dec->second._ecef_y, dec->second._ecef_z,
                                 dec->second._quality, dec->second._sat_num, dec->second._diff);

            // 对于near模式的订阅者，额外触发最近基站切换
            auto near_sub = _base_near_sub_map.find(connect_key);
            if (near_sub != _base_near_sub_map.end())
            {
                // 距离阈值判断：仅当首次或位置变化超过 _near_switch_distance(米) 时才触发最近基站检索，
                // 避免在静止/微小漂移下频繁切换基站
                bool need_query = false;
                auto pos_it = _near_pos_cache_map.find(connect_key);
                if (pos_it == _near_pos_cache_map.end())
                {
                    need_query = true;
                }
                else
                {
                    double dx = dec->second._ecef_x - pos_it->second.ecef_x;
                    double dy = dec->second._ecef_y - pos_it->second.ecef_y;
                    double dz = dec->second._ecef_z - pos_it->second.ecef_z;
                    double dist2 = dx * dx + dy * dy + dz * dz;
                    double thr = _near_switch_distance;
                    if (thr <= 0.0 || dist2 >= thr * thr)
                    {
                        need_query = true;
                    }
                }

                if (need_query)
                {
                    // 将ECEF转换为经纬度，触发最近基站查询
                    double lat = 0.0, lon = 0.0, alt = 0.0;
                    util_ecef2pos(dec->second._ecef_x, dec->second._ecef_y, dec->second._ecef_z, lat, lon, alt);

                    // 更新参考坐标，下次以此点为基准计算距离
                    near_pos_cache &cache = _near_pos_cache_map[connect_key];
                    cache.ecef_x = dec->second._ecef_x;
                    cache.ecef_y = dec->second._ecef_y;
                    cache.ecef_z = dec->second._ecef_z;

                    // 调用sub_near_channel更新最近基站订阅
                    sub_near_channel(near_sub->second.channel.c_str(), near_sub->second.user_name.c_str(),
                                     lat, lon, connect_key, near_sub->second.cb, near_sub->second.arg,
                                     near_sub->second.group_uid.c_str());
                }
            }
        }
    }

    int ret = redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH USR:%s %b", safe_cstr(user_name), data, data_length);
    if (ret != REDIS_OK)
    {
        return finish_redis_publish_failure(CoreResult::failure(CoreErrorCode::PublishFailed,
                                                                __func__,
                                                                "Redis publish command failed")
                                                .with_redis_key(redis_key)
                                                .with_subject(safe_cstr(connect_key)));
    }
    return REDIS_OK;
}

void caster_internal::Redis_Pub_ReconnectCallback(evutil_socket_t fd, short events, void *arg)
{
}

void caster_internal::Redis_Sub_ReconnectCallback(evutil_socket_t fd, short events, void *arg)
{
}

void caster_internal::Redis_Connect_Cb(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK)
    {
        spdlog::critical("[{}:{}]: Redis connection failed: {}, terminating", __class__, __func__, c->errstr);
        exit(1);
        return;
    }
    spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
}

void caster_internal::Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK)
    {
        spdlog::critical("[{}:{}]: Redis unexpected disconnect: {}, terminating", __class__, __func__, c->errstr);
        exit(1);
        return;
    }
    spdlog::info("[{}:{}]: redis info: Disconnected Redis", __class__, __func__);
}

void caster_internal::Redis_Pub_Connect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<caster_internal *>(c->data);

    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
        svr->_is_pub_connected = true;
        svr->init_pub_context();
    }
    else
    {
        svr->_is_pub_connected = false;
        svr->_pub_context_errstr = c->errstr ? c->errstr : "unknown";
        spdlog::error("[{}:{}]: Redis pub connection failed: {}", __class__, __func__, svr->_pub_context_errstr);
        svr->_pub_context = nullptr; /* avoid stale pointer when callback returns */
        return;
    }
    svr->pubAttemptReconnect();
}

void caster_internal::Redis_Sub_Connect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<caster_internal *>(c->data);

    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
        svr->_is_sub_connected = true;
        svr->init_sub_context();
    }
    else
    {
        svr->_is_sub_connected = false;
        svr->_sub_context_errstr = c->errstr ? c->errstr : "unknown";
        spdlog::error("[{}:{}]: Redis sub connection failed: {}", __class__, __func__, svr->_sub_context_errstr);
        svr->_sub_context = nullptr; /* avoid stale pointer when callback returns */
        return;
    }
    svr->subAttemptReconnect();
}

void caster_internal::Redis_Pub_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<caster_internal *>(c->data);

    svr->_is_pub_connected = false;
    svr->_pub_context_errstr = c->err;
    svr->_pub_context = NULL; /* avoid stale pointer when callback returns */
    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: redis info: {}", __class__, __func__, svr->_pub_context_errstr);
    }
    else
    {
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_pub_context_errstr);
        svr->pubAttemptReconnect();
    }
}

void caster_internal::Redis_Sub_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<caster_internal *>(c->data);

    svr->_is_sub_connected = false;
    svr->_sub_context_errstr = c->err;
    svr->_sub_context = NULL; /* avoid stale pointer when callback returns */
    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: redis info: {}", __class__, __func__, svr->_sub_context_errstr);
    }
    else
    {
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_sub_context_errstr);
        svr->subAttemptReconnect();
    }
}

int caster_internal::init_sub_context()
{
    redisAsyncCommand(_sub_context, Redis_Broadcast_Callback, this, "SUBSCRIBE CASTER:BROADCAST");
    redisAsyncCommand(_sub_context, Redis_ConfChange_Callback, this, "SUBSCRIBE CASTER:CONF");
    redisAsyncCommand(_sub_context, Redis_NodeChannel_Callback, this, "SUBSCRIBE NODE:%s", _node_ID.c_str());

    // 重新订阅所有的需要订阅的频道
    for (auto iter : _base_sub_map)
    {
        redisAsyncCommand(_sub_context, Redis_SUB_Base_Callback, this, "SUBSCRIBE MPT:%s", iter.first.c_str());
    }
    // 重新订阅所有的需要订阅的频道
    for (auto iter : _rover_sub_map)
    {
        redisAsyncCommand(_sub_context, Redis_SUB_Rover_Callback, this, "SUBSCRIBE USR:%s", iter.first.c_str());
    }

    return 0;
}

int caster_internal::init_pub_context()
{
    // redisAsyncCommand(_pub_context, NULL, NULL, "DEL " MPT_STATUS_LIST);
    // redisAsyncCommand(_pub_context, NULL, NULL, "DEL " USR_STATUS_LIST);
    download_active_item();
    download_alias_rule();
    download_access_policy();
    return 0;
}

void caster_internal::Redis_Register_Base_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto arg = static_cast<std::pair<caster_internal *, caster_cb_item> *>(privdata);
    auto svr = arg->first;
    auto cb_item = arg->second;

    bool _check = false; // 检验记录中是否包含本条记录
    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::list<std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        if (strcmp(field, cb_item.connect_key.c_str()) == 0)
        {
            _check = true;
        }
        records.push_back(field);
    }

    try
    {
        if (!_check)
        {
            throw std::logic_error("Can't Find Recored"); // 在返回的记录中不包含本条记录
        }

        //_check通过的话，records.size()必定大于等于1
        if (records.size() != 1 && !svr->_base_enable_mult) // 有多个连接记录且设置不允许多个记录
        {
            if (svr->_base_keep_early) // 已在线的优先级高，踢出当前
            {
                throw std::logic_error("Base already online ,don't allow base duplicate logins");
            }

            for (auto iter : records) // 新上线的优先级高，踢出出其他已在线记录
            {
                if (iter != cb_item.connect_key)
                {
                    svr->send_status_base_channel(cb_item.channel.c_str(), iter.c_str(), CasterReply::ERR, "New same name Base Login, kick out this Connect!");
                }
            }
        }
        // else 有1个或者多个连接，但是允许多个记录
        // 正常，返回一个成功回调
        caster_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = "";
        cb_item.cb(NULL, cb_item.arg, &Reply);
    }
    catch (const std::exception &e)
    {
        // std::cerr << e.what() << '\n';
        // 异常，发送关闭当前连接的请求
        svr->send_status_base_channel(cb_item.channel.c_str(), cb_item.connect_key.c_str(), CasterReply::ERR, e.what());
    }

    delete arg;
}

void caster_internal::Redis_Register_Rover_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto arg = static_cast<std::pair<caster_internal *, caster_cb_item> *>(privdata);
    auto svr = arg->first;
    auto cb_item = arg->second;

    bool _check = false; // 检验记录中是否包含本条记录

    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::list<std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        if (strcmp(field, cb_item.connect_key.c_str()) == 0)
        {
            _check = true;
        }
        records.push_back(field);
    }

    try
    {
        if (!_check)
        {
            throw std::logic_error("Can't Find Recored"); // 在返回的记录中不包含本条记录
        }

        //_check通过的话，records.size()必定大于等于1
        if (records.size() != 1 && !svr->_rover_enable_mult) // 有多个连接记录且设置不允许多个记录
        {
            if (svr->_rover_keep_early) // 已在线的优先级高，踢出当前
            {
                throw std::logic_error("Base already online ,don't allow base duplicate logins");
            }

            for (auto iter : records) // 新上线的优先级高，踢出出其他已在线记录
            {
                if (iter != cb_item.connect_key)
                {
                    svr->send_status_rover_channel(cb_item.channel.c_str(), iter.c_str(), CasterReply::ERR, "New same name Rover Login, kick out this Connect!");
                }
            }
        }
        // else 有1个或者多个连接，但是允许多个记录
        // 正常，返回一个成功回调
        caster_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = "";
        cb_item.cb(NULL, cb_item.arg, &Reply);
    }
    catch (const std::exception &e)
    {
        // std::cerr << e.what() << '\n';
        // 异常，发送关闭当前连接的请求
        svr->send_status_rover_channel(cb_item.channel.c_str(), cb_item.connect_key.c_str(), CasterReply::ERR, e.what());
    }

    delete arg;
}

void caster_internal::Redis_SUB_Base_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);
    // auto cb_map = svr->_sub_cb_map;// 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错

    if (!reply)
    {
        return;
    }
    if (reply->elements == 3)
    {
        auto re1 = reply->element[0];
        auto re2 = reply->element[1];
        auto re3 = reply->element[2];

        caster_reply Reply;
        Reply.type = CasterReply::STRING;
        Reply.str = re3->str;
        Reply.len = re3->len;

        char type[64];     // 存储 ':' 之前的部分
        char channel[128]; // 存储 ':' 之后的部分
        if (sscanf(re2->str, "%[^:]:%s", type, channel) != 2)
        {
            // std::cerr << "Input string format is incorrect" << std::endl;
            return;
        }
        auto channel_subs = svr->_base_sub_map.find(channel); // 找到订阅该频道的map
        if (channel_subs == svr->_base_sub_map.end())
        {
            return;
        }
        auto subs = channel_subs->second;
        for (auto iter : subs) // 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错
        {
            auto cb_item = iter.second;
            auto Func = cb_item.cb;
            auto arg = cb_item.arg;
            Func(re2->str, arg, &Reply);

            auto str = svr->_stream_status_map.find(cb_item.connect_key);
            if (str != svr->_stream_status_map.end())
            {
                str->second.add_send(Reply.len);
                svr->add_sum_send(Reply.len);
            }
        }
    }
}

void caster_internal::Redis_SUB_Rover_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);
    // auto cb_map = svr->_sub_cb_map;// 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错

    if (!reply)
    {
        return;
    }
    if (reply->elements == 3)
    {
        auto re1 = reply->element[0];
        auto re2 = reply->element[1];
        auto re3 = reply->element[2];

        caster_reply Reply;
        Reply.type = CasterReply::STRING;
        Reply.str = re3->str;
        Reply.len = re3->len;

        auto channel_subs = svr->_rover_sub_map.find(re2->str); // 找到订阅该频道的map
        if (channel_subs == svr->_rover_sub_map.end())
        {
            return;
        }
        auto subs = channel_subs->second;
        for (auto iter : subs) // 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错
        {
            auto cb_item = iter.second;
            auto Func = cb_item.cb;
            auto arg = cb_item.arg;
            Func(re2->str, arg, &Reply);

            auto str = svr->_stream_status_map.find(cb_item.connect_key);
            if (str != svr->_stream_status_map.end())
            {
                str->second.add_send(Reply.len);
                svr->add_sum_send(Reply.len);
                // if (svr->_upload_base_stat)
                // {
                //     redisAsyncCommand(svr->_pub_context, NULL, NULL, "HSETEX " MPT_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(svr->_key_expire_time).c_str(), cb_item.connect_key.c_str(), str->second.get_status_str(0).c_str());
                // }
            }
        }
    }
}

void caster_internal::Redis_Get_Hash_Field_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto map = static_cast<std::unordered_map<std::string, std::string> *>(privdata);

    if (!reply)
    {
        return;
    }

    map->clear();
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        auto value = reply->element[i + 1]->str;
        map->insert(std::pair<std::string, std::string>(field, value));
    }
}

void caster_internal::Redis_Get_Set_Value_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto set = static_cast<std::set<std::string> *>(privdata);

    if (!reply)
    {
        return;
    }

    set->clear();
    for (int i = 0; i < reply->elements; i++)
    {
        auto value = reply->element[i]->str; // 调试用
        set->insert(value);
    }
}

void caster_internal::Redis_Get_Hash_Lenth_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto count = static_cast<size_t *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_INTEGER)
    {
        *count = static_cast<size_t>(reply->integer);
    }
}

void caster_internal::Redis_Sub_Ping_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);
    svr->_sub_pong_time = std::chrono::high_resolution_clock::now();
    svr->_sub_ping_delay = std::chrono::duration_cast<std::chrono::microseconds>(svr->_sub_pong_time - svr->_sub_ping_time).count();
    svr->_sub_tcp_delay = util_get_tcp_delay(c->c.fd);
    svr->_sub_ping_fail_count = 0;
}

void caster_internal::Redis_Pub_Ping_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);
    svr->_pub_pong_time = std::chrono::high_resolution_clock::now();
    svr->_pub_ping_delay = std::chrono::duration_cast<std::chrono::microseconds>(svr->_pub_pong_time - svr->_pub_ping_time).count();
    svr->_pub_tcp_delay = util_get_tcp_delay(c->c.fd);
    svr->_pub_ping_fail_count = 0;
}

int caster_internal::subAttemptReconnect()
{
    if (_is_sub_connected)
    {
        return 0;
    }

    if (_sub_context)
    {
        redisAsyncDisconnect(_sub_context);
        _sub_context = nullptr;
    }

    // 初始化redis连接
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, _redis_IP.c_str(), _redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 10;
    options.connect_timeout = &tv;

    _sub_context = redisAsyncConnectWithOptions(&options);
    if (_sub_context->err)
    {
        /* Let *c leak for now... */
        spdlog::critical("[{}:{}]: Redis sub reconnect failed: {}, terminating", __class__, __func__, _sub_context->errstr);
        redisAsyncFree(_sub_context);
        _sub_context = nullptr;
        // 直接退出程序
        exit(1);
    }
    _sub_context->data = this;

    redisLibeventAttach(_sub_context, _base);
    redisAsyncSetConnectCallback(_sub_context, Redis_Sub_Connect_Cb);
    redisAsyncSetDisconnectCallback(_sub_context, Redis_Sub_Disconnect_Cb);

    redisAsyncCommand(_sub_context, NULL, NULL, "AUTH %s", _redis_Requirepass.c_str());

    return 0;
}

int caster_internal::pubAttemptReconnect()
{
    if (_is_pub_connected)
    {
        return 0;
    }

    if (_pub_context)
    {
        redisAsyncDisconnect(_pub_context);
        _pub_context = nullptr;
    }

    // 初始化redis连接
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, _redis_IP.c_str(), _redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 10;
    options.connect_timeout = &tv;

    _pub_context = redisAsyncConnectWithOptions(&options);
    if (_pub_context->err)
    {
        /* Let *c leak for now... */
        spdlog::critical("[{}:{}]: Redis pub reconnect failed: {}, terminating", __class__, __func__, _pub_context->errstr);
        redisAsyncFree(_pub_context);
        _pub_context = nullptr;
        // 直接退出程序
        exit(1);
    }
    _pub_context->data = this;

    redisLibeventAttach(_pub_context, _base);
    redisAsyncSetConnectCallback(_pub_context, Redis_Pub_Connect_Cb);
    redisAsyncSetDisconnectCallback(_pub_context, Redis_Pub_Disconnect_Cb);

    redisAsyncCommand(_pub_context, NULL, NULL, "AUTH %s", _redis_Requirepass.c_str());

    return 0;
}

void caster_internal::cleanOld(std::deque<Sample> &history, int64_t now)
{
    while (!history.empty() && now - history.front().time > _windowSize)
    {
        history.pop_front();
    }
}

double caster_internal::calcAvgSpeed(const std::deque<Sample> &history) const
{
    if (history.size() < 2)
        return 0.0;
    const Sample &first = history.front();
    const Sample &last = history.back();
    int64_t deltaTime = last.time - first.time;
    if (deltaTime <= 0)
        return 0.0;
    int64_t deltaBytes = last.bytes - first.bytes;
    return static_cast<double>(deltaBytes) / deltaTime;
}

int caster_internal::add_sum_recv(int size)
{
    _recv_total += size;
    _update_time = util_get_now_second();
    _recvHistory.push_back({_update_time, _recv_total});
    cleanOld(_recvHistory, _update_time);
    _recv_speed = calcAvgSpeed(_recvHistory);
    return 0;
}

int caster_internal::add_sum_send(int size)
{
    _send_total += size;
    _update_time = util_get_now_second();
    _sendHistory.push_back({_update_time, _send_total});
    cleanOld(_sendHistory, _update_time);
    _send_speed = calcAvgSpeed(_sendHistory);
    return 0;
}

void caster_internal::Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 接收广播信息，触发回调执行任务
    // 订阅到的是一个Json字符串

    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }
    if (reply->elements != 3)
    {
        return; // 异常的回调参数
    }

    auto re1 = reply->element[0];
    auto re2 = reply->element[1];
    auto re3 = reply->element[2];

    if (re3->type != REDIS_REPLY_STRING)
    {
        return; // 回复不是字符串，第一次订阅这个频道的时候回应为 REDIS_REPLY_INTEGER
    }

    svr->broadcast_response(re3->str);
}

void caster_internal::Redis_ConfChange_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply || reply->elements != 3)
        return;

    auto re3 = reply->element[2];
    if (re3->type != REDIS_REPLY_STRING)
        return; // first subscribe ack is INTEGER, skip

    std::string topic(re3->str);
    spdlog::debug("[caster_internal]: config change notification: {}", topic);

    if (topic == "ALIAS")
    {
        svr->download_alias_rule();
    }
    else if (topic == "ACCESS")
    {
        svr->download_access_policy();
    }
}

int caster_internal::broadcast_response(std::string req_str)
{
    // 根据接收到的广播，触发对应的回调函数，通知Caster外围创建和删除任务
    broadcast_msg req;
    if (req.fromString(req_str))
    {
        return 1; // 解析失败
    }

    // 强制下线: operate=DELETE 且 target 为具体连接key 时, 直接通过 _kick_map 派发 ERR 回调
    if (req.operate == caster::core::BOARDCAST_OPERATE_DELETE && !req.target.empty()
        && (req.type == caster::core::BOARDCAST_TYPE_SERVER_OPERATE
            || req.type == caster::core::BOARDCAST_TYPE_CLIENT_OPERATE))
    {
        auto kit = _kick_map.find(req.target);
        if (kit != _kick_map.end())
        {
            caster_reply Reply;
            Reply.type = CasterReply::ERR;
            Reply.str = req.reason_str.empty() ? "Force offline by administrator" : req.reason_str.c_str();
            auto cb_item = kit->second;
            cb_item.cb(NULL, cb_item.arg, &Reply);
        }
        return 0;
    }

    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> *item_map = nullptr;

    if (req.type == caster::core::BOARDCAST_TYPE_CLIENT_OPERATE)
    {
        item_map = &_base_register_map;
    }
    else if (req.type == caster::core::BOARDCAST_TYPE_SERVER_OPERATE)
    {
        item_map = &_rover_register_map;
    }
    else
    {
        return 1; // 不支持的广播类型
    }
    // 收到状态更新的请求
    auto item = item_map->find(req.msg_str); // 状态变更时msg_str中存储的是channel
    if (item == item_map->end())
    {
        return 2; // 本地没有该频道的注册记录
    }

    // 复制字符串
    caster_reply Reply;
    Reply.type = broadcast_msg::OperateToReply(req.operate);
    Reply.str = req.reason_str.c_str();

    if (req.target.empty()) // 没有指定特定的连接，则对所有的连接都发送一次回复（针对允许同名频道都在线的情况）
    {
        for (auto iter : item->second)
        {
            auto cb_item = iter.second;
            auto Func = cb_item.cb;
            auto arg = cb_item.arg;
            Func(NULL, arg, &Reply);
        }
    }
    else
    {
        auto target = item->second.find(req.target);
        if (target == item->second.end())
        {
            return 3; // 本地没有该连接的注册记录
        }

        auto cb_item = target->second;
        auto Func = cb_item.cb;
        auto arg = cb_item.arg;
        Func(NULL, arg, &Reply);
    }

    return 0;
}

void caster_internal::Redis_Update_Active_Base_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL MPT:LIST: reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL MPT:LIST reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    // if (reply->elements == 0)
    // {
    //     spdlog::info("[{}:{}]: HGETALL MPT:LIST reply->elements: {}", __class__, __func__, reply->elements);
    //     return;
    // }

    svr->_active_mount_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        svr->_active_mount_map.insert(std::pair<std::string, std::string>(field, value));
    }

    svr->check_active_base_channel();

    // spdlog::info("Sync active base, current item:{} ", svr->_active_mount_set.size());
}

void caster_internal::Redis_Update_Active_Rover_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("HGETALL USR:LIST: reply->type == REDIS_REPLY_NIL");
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("HGETALL USR:LIST reply->type != REDIS_REPLY_ARRAY: {}", reply->type);
        return;
    }

    svr->_active_user_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        svr->_active_user_map.insert(std::pair<std::string, std::string>(field, value));
    }
    // spdlog::info("Sync active rover, current item:{} ", svr->_active_user_set.size());

    svr->check_active_rover_channel(); // 检测活跃基站频道(如果已经不存在, 那么就踢出本地连接)
}

void caster_internal::Redis_Update_Decode_Source_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL MPT:SOURCE: reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL MPT:SOURCE reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    // if (reply->elements == 0)
    // {
    //     spdlog::info("[{}:{}]: HGETALL MPT:SOURCE reply->elements: {}", __class__, __func__, reply->elements);
    //     return;
    // }

    svr->_source_decode_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        source_record record(field);
        record.fromString(value);
        svr->_source_decode_map.insert(std::pair<std::string, source_record>(field, record));
    }
}

void caster_internal::Redis_Update_Record_Source_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL MPT:RECORD: reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL MPT:RECORD reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    // if (reply->elements == 0)
    // {
    //     spdlog::info("[{}:{}]: HGETALL MPT:RECORD reply->elements: {}", __class__, __func__, reply->elements);
    //     return;
    // }

    svr->_source_record_map.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        source_record record(field);
        record.fromString(value);
        svr->_source_record_map.insert(std::pair<std::string, source_record>(field, record));
    }
}

void caster_internal::Redis_Update_Alias_Rule_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("HGETALL ALIAS:RULE reply->type == REDIS_REPLY_NIL");
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("HGETALL ALIAS:RULE reply->type != REDIS_REPLY_ARRAY: {}", reply->type);
        return;
    }

    svr->_alias_rule_map.clear();
    svr->_alias_visible_map.clear();

    for (size_t i = 0; i + 1 < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        // Try to parse as JSON (new format from web API)
        try
        {
            auto j = nlohmann::json::parse(value);
            if (j.is_object() && j.contains("alias_name") && j.contains("source_name"))
            {
                std::string alias_name = j.value("alias_name", "");
                std::string source_name = j.value("source_name", "");
                bool enable = j.value("enable", true);
                bool visible = j.value("visible", true);

                if (!alias_name.empty() && !source_name.empty())
                {
                    if (enable)
                    {
                        svr->_alias_rule_map[alias_name].push_back(source_name);
                    }
                    if (visible)
                    {
                        svr->_alias_visible_map[alias_name] = source_name;
                    }
                }
                continue;
            }
        }
        catch (...) {}

        // Fallback: legacy format (semicolon-separated source names)
        std::list<std::string> values = util_split_string(value, ';');
        svr->_alias_rule_map.insert(std::pair<std::string, std::list<std::string>>(field, values));
    }

    // 查找实体基站是否有和别名基站重名的，如果有，那么要踢出实体基站，以别名基站为准
    // svr->check_alias_rule_conflict();
}

void caster_internal::Redis_Update_Access_Group_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("HGETALL ACCESS:GROUP reply->type == REDIS_REPLY_NIL");
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("HGETALL ACCESS:GROUP reply->type != REDIS_REPLY_ARRAY: {}", reply->type);
        return;
    }

    std::unordered_map<std::string, access_group> access_group_map;
    std::list<std::string> group_uids;

    for (size_t i = 0; i + 1 < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        access_group group(field);
        if (group.fromString(value) != 0)
        {
            spdlog::warn("[{}:{}]: skip invalid access group [{}]", __class__, __func__, field);
            continue;
        }
        group_uids.push_back(group.uid());
        access_group_map.insert(std::pair<std::string, access_group>(group.uid(), group));
    }

    svr->_access_group_map = std::move(access_group_map);
    svr->_access_item_map.clear();

    for (const auto &group_uid : group_uids)
    {
        auto ctx = new std::pair<caster_internal *, std::string>(svr, group_uid);
        redisAsyncCommand(svr->_pub_context, Redis_Update_Access_Item_Callback, ctx,
                          "HGETALL " ACCESS_ITEM ":%s", group_uid.c_str());
    }
}

void caster_internal::Redis_Update_Access_Item_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    std::unique_ptr<std::pair<caster_internal *, std::string>> ctx(static_cast<std::pair<caster_internal *, std::string> *>(privdata));
    auto reply = static_cast<redisReply *>(r);

    if (!ctx || !reply)
    {
        return;
    }

    auto svr = ctx->first;
    const auto group_uid = ctx->second;

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("HGETALL ACCESS:ITEM:{} reply->type == REDIS_REPLY_NIL", group_uid);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("HGETALL ACCESS:ITEM:{} reply->type != REDIS_REPLY_ARRAY: {}", group_uid, reply->type);
        return;
    }

    std::unordered_map<std::string, access_item> access_items;
    for (size_t i = 0; i + 1 < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        access_item item(field);
        if (item.fromString(value) != 0)
        {
            spdlog::warn("[{}:{}]: skip invalid access item [{}] for group [{}]", __class__, __func__, field, group_uid);
            continue;
        }
        access_items.insert(std::pair<std::string, access_item>(item.mount_point_name(), item));
    }

    svr->_access_item_map[group_uid] = std::move(access_items);
}

void caster_internal::Redis_Geo_Radius_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    std::unique_ptr<std::string> connect_key(static_cast<std::string *>(privdata));

    if (!connect_key)
    {
        return;
    }

    auto *svr = caster_internal::getInstance();
    auto near_item = svr->_base_near_sub_map.find(*connect_key);
    if (near_item == svr->_base_near_sub_map.end())
    {
        return;
    }
    auto *cb_item = &near_item->second;

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: GEORADIUS MPT:GEO reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: GEORADIUS MPT:GEO reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    // 查找成功
    for (int i = 0; i < reply->elements; i++)
    {
        auto reply_item = reply->element[i];

        auto field = reply_item->element[0]->str;
        auto value = reply_item->element[1]->str;

        // 判断所有符合要求的挂载点
        auto iter = svr->_active_mount_map.find(field); // 查找这个挂载点是否处于在线状态
        if (iter == svr->_active_mount_map.end())       // 如果不在线，那么要把这个记录删掉
        {
            redisAsyncCommand(svr->_pub_context, NULL, NULL, "ZREM " MPT_POSITION_LIST " %s", field);
            continue;
        }
        else // 如果在线,订阅这个挂载点
        {
            // 判断这个挂载点和当前订阅的挂载点是同一个，那么就跳过
            if (cb_item->channel == field)
            {
                // 已经订阅了这个挂载点，跳过
                return;
            }

            // 如果不一致，要先把旧的订阅移除，然后添加到新的订阅上

            // 查询当前connect_key是否已经有订阅站点，
            auto sub_base_item = svr->_base_sub_map.find(cb_item->channel);
            if (sub_base_item != svr->_base_sub_map.end()) // 没有这个订阅记录
            {
                auto sub_item = sub_base_item->second.find(cb_item->connect_key);
                if (sub_item != sub_base_item->second.end())
                {
                    // 找到这个订阅记录，取消订阅
                    svr->_base_sub_map[cb_item->channel].erase(cb_item->connect_key);
                }
            }

            // 更新用户订阅的挂载点信息
            auto item = svr->_client_status_map.find(cb_item->connect_key);
            if (item != svr->_client_status_map.end())
            {
                item->second.set_alias_mpt(field);
            }

            // 添加到新的订阅上去
            cb_item->channel = field;
            svr->sub_base_channel(field, cb_item->user_name.c_str(), cb_item->connect_key.c_str(), cb_item->cb, cb_item->arg, cb_item->group_uid.c_str(), true);

            return;
        }
    }

    caster_reply Reply;
    Reply.type = CasterReply::ERR;
    Reply.str = "Can't Find Useful Nearest Mount Point"; // 实际使用的挂载点
    Reply.dval = 0.0;                                    // 距离
    cb_item->cb(NULL, cb_item->arg, &Reply);
}



// ======================== helper ========================

static int resolve_register_type(CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
        return 1;
    case CasterRegisterType::CLIENT:
        return 1;
    case CasterRegisterType::NEAREST:
        return 2;
    case CasterRegisterType::ALIAS:
        return 7;
    case CasterRegisterType::PULL:
        return 3;
    case CasterRegisterType::PUSH:
        return 3;
    default:
        return 0;
    }
}
