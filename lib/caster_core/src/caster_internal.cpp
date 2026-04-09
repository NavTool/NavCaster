#include "caster_internal.h"
#include <chrono>
#include <list>
// #include <format>
#include <spdlog/spdlog.h>
#include <sstream>
#include "knt.h"
#include "SysUsage.h"
#include "version.h"

#define __class__ "caster_internal"

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

int caster_internal::start()
{

    _startup_time = util_get_now_second();

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
    redisAsyncDisconnect(_sub_context);
    redisAsyncFree(_sub_context);
    redisAsyncDisconnect(_pub_context);
    redisAsyncFree(_pub_context);
    return 0;
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

bool caster_internal::is_nearest_mpt(std::string mount_point)
{
    if (mount_point == "NEAREST")
    {
        return true;
    }

    return false;
}

bool caster_internal::is_alias_mpt(std::string mount_point)
{

    // 从alias列表中查找

    return false;
}

// ==================== 统一接口实现（按 CasterRegisterType 分发） ====================

int caster_internal::register_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return register_base_channel(channel, user_name, connect_key, cb, arg, type);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::PUSH:
        return register_rover_channel(channel, user_name, connect_key, cb, arg, type);
    default:
        return -1;
    }
}

int caster_internal::withdraw_channel(const char *channel, const char *user_name, const char *connect_key, CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return withdraw_base_channel(channel, user_name, connect_key);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::PUSH:
        return withdraw_rover_channel(channel, user_name, connect_key);
    default:
        return -1;
    }
}

int caster_internal::pub_channel(const char *mount_point, const char *user_name, const char *connect_key, const char *data, size_t data_length, CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return pub_base_channel(mount_point, connect_key, data, data_length);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::PUSH:
        return pub_rover_channel(user_name, connect_key, data, data_length);
    default:
        return -1;
    }
}

int caster_internal::sub_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type, double lat, double lon)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        // 基站类型订阅的是移动站数据
        return sub_rover_channel(channel, user_name, connect_key, cb, arg);
    case CasterRegisterType::CLIENT:
        // 移动站订阅基站数据
        return sub_base_channel(channel, user_name, connect_key, cb, arg);
    case CasterRegisterType::NEAREST:
        // 最近挂载点模式
        return sub_near_channel(channel, user_name, lat, lon, connect_key, cb, arg);
    case CasterRegisterType::ALIAS:
        // 别名挂载点模式
        return sub_alias_channel(channel, user_name, connect_key, cb, arg);
    case CasterRegisterType::PUSH:
        // 推送模式订阅基站数据
        return sub_base_channel(channel, user_name, connect_key, cb, arg);
    default:
        return -1;
    }
}

int caster_internal::unsub_channel(const char *channel, const char *user_name, const char *connect_key, CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        // 基站类型取消订阅移动站数据
        return unsub_rover_channel(user_name, connect_key);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::ALIAS:
    case CasterRegisterType::PUSH:
        // 移动站类型取消订阅基站数据
        return unsub_base_channel(channel, connect_key);
    default:
        return -1;
    }
}

int caster_internal::set_coord_info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, CasterRegisterType type, int Q, int sat, double diff)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return set_base_coord_info(mount_point, connect_key, ecef_x, ecef_y, ecef_z);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::PUSH:
        return set_rover_coord_info(mount_point, connect_key, ecef_x, ecef_y, ecef_z, Q, sat, diff);
    default:
        return -1;
    }
}

int caster_internal::update_relay_info(const char *mount_point, const char *alias_mpt, const char *connect_key, int state, CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return update_pull_base_info(mount_point, alias_mpt, connect_key, state);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::PUSH:
        return update_push_rover_info(mount_point, alias_mpt, connect_key, state);
    default:
        return -1;
    }
}

// ==================== 基站/移动站频道内部实现 ====================

int caster_internal::sub_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
{
    try
    {
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

int caster_internal::sub_near_channel(const char *channel, const char *user_name, double lat, double lon, const char *connect_key, CasterCallback cb, void *arg)
{
    // 查找是否是已经订阅过最近基站
    auto find = _base_near_sub_map.find(connect_key);
    if (find == _base_near_sub_map.end())
    {
        // 还没有订阅过，添加一条记录到map中
        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.cb = cb;
        cb_item.arg = arg;
        _base_near_sub_map.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));
    }
    else
    {
        // 已经订阅过，更新回调函数和参数
        find->second.cb = cb;
        find->second.arg = arg;
    }

    find = _base_near_sub_map.find(connect_key);
    caster_cb_item *ptr = &find->second;
    // 添加一个查询，查询最近的站点

    redisAsyncCommand(_pub_context, Redis_Geo_Radius_Callback, ptr, "GEORADIUS " MPT_POSITION_LIST " %s %s 100 KM WITHDIST ASC", std::to_string(lon).c_str(), std::to_string(lat).c_str());

    return 0;
}

int caster_internal::sub_alias_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
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
            return caster_internal::getInstance()->sub_base_channel(alias_mpt.c_str(), user_name, connect_key, cb, arg);
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

    auto near_item = _base_near_sub_map.find(connect_key);
    if (near_item != _base_near_sub_map.end()) // 如果这个订阅是使用的最近基站订阅模式，那么就删除这个记录
    {
        _base_near_sub_map.erase(near_item);
    }

    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " MPT_SUBSCRIBE_LIST ":%s %s", channel, connect_key);

    return 0;
}

int caster_internal::sub_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
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

    return 0;
}

int caster_internal::set_connect_delay_info(const char *connect_key, uint64_t delay)
{
    auto item = _server_status_map.find(connect_key);
    if (item != _server_status_map.end())
    {
        item->second.add_delay(delay);
        return 0;
    }
    auto item2 = _client_status_map.find(connect_key);
    if (item2 != _client_status_map.end())
    {
        item2->second.add_delay(delay);
        return 0;
    }
    return 1;
}

std::string caster_internal::get_source_list_text()
{
    std::string str;
    for (auto iter : _source_decode_map)
    {
        str += iter.second.toSourceItem();
    }

    return str;
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

int caster_internal::check_redis_connection()
{
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

int caster_internal::update_pull_base_info(const char *mount_point, const char *alias_mpt, const char *connect_key, int state)
{
    auto stat_item = _server_status_map.find(connect_key);
    if (stat_item != _server_status_map.end())
    {
        stat_item->second.set_alias_mpt(alias_mpt);
    }

    auto pull_item = _pull_status_map.find(mount_point);
    if (pull_item != _pull_status_map.end())
    {
        pull_item->second.update_state(connect_key, state);
    }
    return 0;
}

int caster_internal::update_push_rover_info(const char *mount_point, const char *alias_mpt, const char *connect_key, int state)
{
    auto stat_item = _client_status_map.find(connect_key);
    if (stat_item != _client_status_map.end())
    {
        stat_item->second.set_alias_mpt(alias_mpt);
    }

    auto push_item = _push_status_map.find(mount_point);
    if (push_item != _push_status_map.end())
    {
        push_item->second.update_state(connect_key, state);
    }
    return 0;
}

int caster_internal::upload_node_status()
{
    // 刷新一下速度
    add_sum_recv(0);
    add_sum_send(0);

    // 将本节点的信息上传到Redis
    caster_node node(_node_ID, _node_name);

    node.set_sys_usage();
    node.set_delay_info(_queue_delay, _sub_ping_delay, _sub_tcp_delay, _pub_ping_delay, _pub_tcp_delay);
    node.set_traffic_info(_send_total, _send_speed, _recv_total, _recv_speed);
    node.set_connection_count(_server_status_map.size(), _client_status_map.size());

    redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " CASTER_NODE_INFO_LIST " EX %s FIELDS 1 %s %s",
                      std::to_string(_key_expire_time).c_str(),
                      _node_ID.c_str(),
                      node.toString().c_str());

    return 0;
}

int caster_internal::try_set_master_node()
{
    redisAsyncCommand(_pub_context, NULL, NULL, "SET " CASTER_MASTER_KEY " %s NX EX %s", _node_ID.c_str(), std::to_string(_key_expire_time).c_str()); // 节点名   NODE为随机字符串+启动后从Redis中获取一个累加值
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
    // 将需要创建的任务 和需要停止的任务，通过广播的形式播发到指定的节点上

    // 查找所有的LIST任务
    for (auto &list_iter : _push_record_map)
    {
        auto stat_iter = _push_status_map.find(list_iter.first);
        if (stat_iter == _push_status_map.end())
        {
            // STAT中不包含这个任务，创建任务
            broadcast_msg msg;
            msg.type = caster::core::BOARDCAST_TYPE_RUSH_OPERATE;
            msg.operate = caster::core::BOARDCAST_OPERATR_ACTIVE;
            msg.target = list_iter.first;              // target填充UID
            msg.msg_str = list_iter.second.toString();  // msg_str为PushRecord的JSON
            msg.reason_str = "Push Task Active";

            // 向某个节点发送广播，当前默认选择主节点执行这个任务
            redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH NODE:%s %s", _node_ID.c_str(), msg.toString().c_str());
        }
    }

    // 查找STAT中是否有多余的任务（在LIST中不存在的）
    for (auto &stat_iter : _push_status_map)
    {
        auto list_iter = _push_record_map.find(stat_iter.first);
        if (list_iter == _push_record_map.end())
        {
            // LIST中不包含这个任务，移除任务
            broadcast_msg msg;
            msg.type = caster::core::BOARDCAST_TYPE_RUSH_OPERATE;
            msg.operate = caster::core::BOARDCAST_OPERATR_INACTIVE;
            msg.target = stat_iter.first;              // target填充UID
            msg.msg_str = stat_iter.second.toString();  // msg_str为PushStatus的JSON
            msg.reason_str = "Push Task Inactive";

            // 向指定节点发送广播
            redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH NODE:%s %s", _node_ID.c_str(), msg.toString().c_str());
        }
    }

    return 0;
}

int caster_internal::relay_pull_task_distribution()
{
    // 将需要创建的任务 和需要停止的任务，通过广播的形式播发到指定的节点上

    // 查找所有的LIST任务
    for (auto &list_iter : _pull_record_map)
    {
        auto stat_iter = _pull_status_map.find(list_iter.first);
        if (stat_iter == _pull_status_map.end())
        {
            // STAT中不包含这个任务，创建任务
            broadcast_msg msg;
            msg.type = caster::core::BOARDCAST_TYPE_PULL_OPERATE;
            msg.operate = caster::core::BOARDCAST_OPERATR_ACTIVE;
            msg.target = list_iter.first;              // target填充UID
            msg.msg_str = list_iter.second.toString();  // msg_str为PullRecord的JSON
            msg.reason_str = "Pull Task Active";

            // 向某个节点发送广播，当前默认选择主节点执行这个任务
            redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH NODE:%s %s", _node_ID.c_str(), msg.toString().c_str());
        }
    }

    // 查找STAT中是否有多余的任务（在LIST中不存在的）
    for (auto &stat_iter : _pull_status_map)
    {
        auto list_iter = _pull_record_map.find(stat_iter.first);
        if (list_iter == _pull_record_map.end())
        {
            // LIST中不包含这个任务，移除任务
            broadcast_msg msg;
            msg.type = caster::core::BOARDCAST_TYPE_PULL_OPERATE;
            msg.operate = caster::core::BOARDCAST_OPERATR_INACTIVE;
            msg.target = stat_iter.first;              // target填充UID
            msg.msg_str = stat_iter.second.toString();  // msg_str为PullStatus的JSON
            msg.reason_str = "Pull Task Inactive";

            // 向指定节点发送广播
            redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH NODE:%s %s", _node_ID.c_str(), msg.toString().c_str());
        }
    }

    return 0;
}

static CasterRelayMsg toRelayMsg(const broadcast_msg &msg)
{
    CasterRelayMsg relay;
    switch (msg.type)
    {
    case caster::core::BOARDCAST_TYPE_SERVER_OPERATE: relay.type = CasterRelayType::SERVER_OPERATE; break;
    case caster::core::BOARDCAST_TYPE_CLIENT_OPERATE: relay.type = CasterRelayType::CLIENT_OPERATE; break;
    case caster::core::BOARDCAST_TYPE_RUSH_OPERATE:   relay.type = CasterRelayType::PUSH_OPERATE;   break;
    case caster::core::BOARDCAST_TYPE_PULL_OPERATE:   relay.type = CasterRelayType::PULL_OPERATE;   break;
    default: relay.type = CasterRelayType::UNSPECIFIED; break;
    }
    switch (msg.operate)
    {
    case caster::core::BOARDCAST_OPERATE_CREATE:    relay.operate = CasterRelayOperate::CREATE;   break;
    case caster::core::BOARDCAST_OPERATE_UPDATE:    relay.operate = CasterRelayOperate::UPDATE;   break;
    case caster::core::BOARDCAST_OPERATE_DELETE:    relay.operate = CasterRelayOperate::DELETE;   break;
    case caster::core::BOARDCAST_OPERATR_ACTIVE:    relay.operate = CasterRelayOperate::ACTIVE;   break;
    case caster::core::BOARDCAST_OPERATR_INACTIVE:  relay.operate = CasterRelayOperate::INACTIVE; break;
    default: relay.operate = CasterRelayOperate::UNSPECIFIED; break;
    }
    relay.target = msg.target;
    relay.msg_str = msg.msg_str;
    relay.reason_str = msg.reason_str;
    return relay;
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
            _pull_status_map.insert({uid, stat});
            _relay_cb(_relay_cb_arg, toRelayMsg(req));
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATR_INACTIVE)
        {
            auto it = _pull_status_map.find(uid);
            if (it == _pull_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, toRelayMsg(req));
            _pull_status_map.erase(it);
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " PULL_STREAM_STATUS " %s", uid.c_str());
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATE_UPDATE)
        {
            auto it = _pull_status_map.find(uid);
            if (it == _pull_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, toRelayMsg(req));
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
            _push_status_map.insert({uid, stat});
            _relay_cb(_relay_cb_arg, toRelayMsg(req));
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATR_INACTIVE)
        {
            auto it = _push_status_map.find(uid);
            if (it == _push_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, toRelayMsg(req));
            _push_status_map.erase(it);
            redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " PUSH_STREAM_STATUS " %s", uid.c_str());
        }
        else if (req.operate == caster::core::BOARDCAST_OPERATE_UPDATE)
        {
            auto it = _push_status_map.find(uid);
            if (it == _push_status_map.end())
            {
                return 3; // 不存在这个任务，忽略
            }
            _relay_cb(_relay_cb_arg, toRelayMsg(req));
        }
    }

    return 0;
}

int caster_internal::upload_relay_status()
{
    for (auto iter : _pull_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " PULL_STREAM_STATUS " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter.first.c_str(), iter.second.toString().c_str());
    }
    for (auto iter : _push_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " PUSH_STREAM_STATUS " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter.first.c_str(), iter.second.toString().c_str());
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

    // 如果返回的节点名和自己的节点名是一致的，那么给这个节点续期
    redisAsyncCommand(svr->_pub_context, Redis_KeepMaster_Callback, svr, "SET " CASTER_MASTER_KEY " %sIFEQ %sEX %s",
                      svr->_node_ID.c_str(),
                      svr->_node_ID.c_str(),
                      std::to_string(svr->_key_expire_time).c_str()); //

    // 如果自己已经不是主节点，那么要清理本地维护的主节点状态信息
}

void caster_internal::Redis_KeepMaster_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    // Master节点续期成功

    // 开始执行节点任务
    svr->sync_cluster_state();
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
        svr->_pull_status_map.insert(std::pair<std::string, pull_status>(field, item));
    }
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
        svr->_push_status_map.insert(std::pair<std::string, push_status>(field, item));
    }
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
    // 基站状态   ConnectKey/基站状态
    for (auto &str : _server_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.toString().c_str());
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " SOURCE_DECODE_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.toSource().c_str());
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " STR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.streamToString().c_str());
    }

    // 用户状态   ConnectKey/用户状态
    for (auto &str : _client_status_map)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.toString().c_str());
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " STR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), str.first.c_str(), str.second.streamToString().c_str());
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

    return 0;
}

int caster_internal::download_active_item()
{
    redisAsyncCommand(_pub_context, Redis_Update_Active_Base_Callback, this, "HGETALL " MPT_ONLINE_LIST);
    redisAsyncCommand(_pub_context, Redis_Update_Active_Rover_Callback, this, "HGETALL " USR_ONLINE_LIST);
    // redisAsyncCommand(_pub_context, Redis_Update_Active_Rover_Callback, this, "HGETALL " USR_ONLINE_LIST);
    // redisAsyncCommand(_pub_context, Redis_Update_Active_Rover_Callback, this, "HGETALL " USR_ONLINE_LIST);

    redisAsyncCommand(_pub_context, Redis_Get_Hash_Lenth_Callback, &_server_connection_count, "HLEN " MPT_STATUS_LIST);
    redisAsyncCommand(_pub_context, Redis_Get_Hash_Lenth_Callback, &_client_connection_count, "HLEN " USR_STATUS_LIST);
    redisAsyncCommand(_pub_context, Redis_Get_Hash_Lenth_Callback, &_pull_connection_count, "HLEN " PULL_STREAM_STATUS);
    redisAsyncCommand(_pub_context, Redis_Get_Hash_Lenth_Callback, &_push_connection_count, "HLEN " PUSH_STREAM_STATUS);
    return 0;
}

int caster_internal::download_alias_rule()
{
    redisAsyncCommand(_pub_context, Redis_Update_Alias_Rule_Callback, this, "HGETALL " ALIAS_RULE_LIST);

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

int caster_internal::register_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type)
{
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

        _server_status_map.insert(std::pair<std::string, server_status>(connect_key, conn));
        // // 向云端插入记录

        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " MPT_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, conn.toString().c_str());

        // 将cb注册回调记录到本地
        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
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

int caster_internal::register_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type)
{
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

        _client_status_map.insert(std::pair<std::string, client_status>(connect_key, conn));

        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX " USR_STATUS_LIST " EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, conn.toString().c_str()); // 更新挂载点数据生产者的更新时间

        // 将cb注册回调记录到本地
        caster_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.cb = cb;
        cb_item.arg = arg;

        if (find->second.find(connect_key) != find->second.end())
        {
            throw std::invalid_argument("Connect_Key is already in the register map");
        }
        find->second.insert(std::pair<std::string, caster_cb_item>(connect_key, cb_item));

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
    // // 向云端插入记录
    if (_upload_base_stat)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " MPT_STATUS_LIST " %s ", connect_key);
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
    // 向云端插入记录
    if (_upload_rover_stat)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL " USR_STATUS_LIST " %s", connect_key);
    }

    return 0;
}

int caster_internal::send_status_base_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason)
{
    // 向redis发布广播
    broadcast_msg item;
    item.type = caster::core::BOARDCAST_TYPE_SERVER_OPERATE;
    item.operate = broadcast_msg::ReplyToOperate(status);
    item.target = connect_key;
    item.msg_str = channel;       // 状态变更时msg_str填充channel
    item.reason_str = reason;

    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH CASTER:BROADCAST %s", item.toString().c_str());
}

int caster_internal::pub_base_channel(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    auto str = _server_status_map.find(connect_key);
    if (str != _server_status_map.end())
    {
        str->second.add_recv(data, data_length);
        add_sum_recv(data_length);
    }
    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH MPT:%s %b", mount_point, data, data_length);
}

int caster_internal::send_status_rover_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason)
{
    // 向redis发布广播
    broadcast_msg item;
    item.type = caster::core::BOARDCAST_TYPE_CLIENT_OPERATE;
    item.operate = broadcast_msg::ReplyToOperate(status);
    item.target = connect_key;
    item.msg_str = channel;       // 状态变更时msg_str填充channel
    item.reason_str = reason;

    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH CASTER:BROADCAST %s", item.toString().c_str());
}

int caster_internal::pub_rover_channel(const char *user_name, const char *connect_key, const char *data, size_t data_length)
{
    auto str = _client_status_map.find(connect_key);
    if (str != _client_status_map.end())
    {
        str->second.add_recv(data, data_length);
        add_sum_recv(data_length);
    }

    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH USR:%s %b", user_name, data, data_length);
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
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, c->errstr);
        // 直接退出程序
        exit(1);
        return;
    }
    spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
}

void caster_internal::Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    if (status != REDIS_OK)
    {
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, c->errstr);
        // 直接退出程序
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
        svr->_pub_context_errstr = c->err;
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_pub_context_errstr);
        svr->_pub_context = nullptr; /* avoid stale pointer when callback returns */

        exit(1);
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
        svr->_sub_context_errstr = c->err;
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_sub_context_errstr);
        svr->_sub_context = nullptr; /* avoid stale pointer when callback returns */

        exit(1);
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

            auto str = svr->_client_status_map.find(cb_item.connect_key);
            if (str != svr->_client_status_map.end())
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

            auto str = svr->_server_status_map.find(cb_item.connect_key);
            if (str != svr->_server_status_map.end())
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
        spdlog::error("redis eror: {}", _sub_context->errstr);
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
        spdlog::error("redis eror: {}", _pub_context->errstr);
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

int caster_internal::broadcast_response(std::string req_str)
{
    // 根据接收到的广播，触发对应的回调函数，通知Caster外围创建和删除任务
    broadcast_msg req;
    if (req.fromString(req_str))
    {
        return 1; // 解析失败
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
        svr->_source_decode_map.insert(std::pair<std::string, std::string>(field, value));
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
        svr->_source_record_map.insert(std::pair<std::string, std::string>(field, value));
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

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        // value是用分号分隔的字符串, 需要拆分成list
        std::list<std::string> values = util_split_string(value, ';');
        svr->_alias_rule_map.insert(std::pair<std::string, std::list<std::string>>(field, values));
    }

    // 查找实体基站是否有和别名基站重名的，如果有，那么要踢出实体基站，以别名基站为准
    // svr->check_alias_rule_conflict();
}

void caster_internal::Redis_Geo_Radius_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto cb_item = static_cast<caster_cb_item *>(privdata);

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
        auto iter = caster_internal::getInstance()->_active_mount_map.find(field); // 查找这个挂载点是否处于在线状态
        if (iter == caster_internal::getInstance()->_active_mount_map.end())       // 如果不在线，那么要把这个记录删掉
        {
            redisAsyncCommand(caster_internal::getInstance()->_pub_context, NULL, NULL, "ZREM " MPT_POSITION_LIST " %s", field);
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
            auto sub_base_item = caster_internal::getInstance()->_base_sub_map.find(cb_item->channel);
            if (sub_base_item != caster_internal::getInstance()->_base_sub_map.end()) // 没有这个订阅记录
            {
                auto sub_item = sub_base_item->second.find(cb_item->connect_key);
                if (sub_item != sub_base_item->second.end())
                {
                    // 找到这个订阅记录，取消订阅
                    caster_internal::getInstance()->_base_sub_map[cb_item->channel].erase(cb_item->connect_key);
                }
            }

            // 更新用户订阅的挂载点信息
            auto item = caster_internal::getInstance()->_client_status_map.find(cb_item->connect_key);
            if (item != caster_internal::getInstance()->_client_status_map.end())
            {
                item->second.set_alias_mpt(field);
            }

            // 添加到新的订阅上去
            cb_item->channel = field;
            caster_internal::getInstance()->sub_base_channel(field, cb_item->user_name.c_str(), cb_item->connect_key.c_str(), cb_item->cb, cb_item->arg);

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
