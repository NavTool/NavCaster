#include "caster_internal.h"
#include <chrono>
#include <list>
// #include <format>
#include <spdlog/spdlog.h>
#include <sstream>
#include "knt.h"

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

int caster_internal::init(json conf, event_base *base)
{
    _update_intv = conf["Update_Intv"];
    _unactive_time = conf["Unactive_Time"];
    _key_expire_time = conf["Key_Expire_Time"];

    _upload_base_stat = conf["Upload_Base_Stat"];
    _upload_rover_stat = conf["Upload_Rover_Stat"];

    _download_base_stat = conf["Download_Base_Stat"];
    _download_rover_stat = conf["Download_Rover_Stat"];

    _base_enable_mult = conf["Base_Enable_Mult"];
    _base_keep_early = conf["Base_Keep_Early"];

    _rover_enable_mult = conf["Rover_Enable_Mult"];
    _rover_keep_early = conf["Rover_Keep_Early"];

    _notify_base_inactive = conf["Notify_Base_Inactive"];
    _notify_rover_inactive = conf["Notify_Rover_Inactive"];

    _redis_IP = conf["Redis_IP"];
    _redis_port = conf["Redis_Port"];
    _redis_Requirepass = conf["Redis_Requirepass"];

    _base = base;
    _updatetime_int = util_get_time_stamp();
    _updatetime_str = util_get_time_stamp_str().c_str();
    return 0;
}

int caster_internal::start()
{
    pubAttemptReconnect();
    subAttemptReconnect();

    _timeout_tv.tv_sec = _update_intv;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);
    event_add(_timeout_ev, &_timeout_tv);

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
                      std::to_string(_active_mount_set.size() + _active_user_set.size()) +
                      ", Active Server: " +
                      std::to_string(_active_mount_set.size()) +
                      ", Active Client: " +
                      std::to_string(_active_user_set.size());

    return str;

    // return std::format("Connection: {}, Active Server: {}, Active Client: {}", , _active_mount_set.size(), _active_user_set.size());
}

int caster_internal::sub_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
{
    try
    {
        if (_active_mount_set.find(channel) == _active_mount_set.end()) // 不是活跃频道
        {
            throw std::logic_error("Can't Find Sub Base Recored");
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
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX MPT:SUB:%s EX %s FIELDS 1 %s %s",
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
        auto item = _rover_status_map.find(connect_key);
        if (item != _rover_status_map.end())
        {
            item->second.set_alias_mpt(channel);
        }

        catser_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = "";
        cb(NULL, arg, &Reply);
    }
    catch (const std::exception &e)
    {
        catser_reply Reply;
        Reply.type = CasterReply::ERR;
        Reply.str = e.what();
        cb(NULL, arg, &Reply);
    }

    return 0;
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
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL MPT:SUB:%s %s", channel, connect_key);

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
            send_status_base_channel(channel, "", CasterReply::ACTIVE, "First subscribe in one Caster Node");
        }
        find = _rover_sub_map.find(channel);

        // 更新订阅者列表
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX USR:SUB:%s EX %s FIELDS 1 %s %s",
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

        catser_reply Reply;
        Reply.type = CasterReply::OK;
        Reply.str = "";
        cb(NULL, arg, &Reply);
    }
    catch (const std::exception &e)
    {
        catser_reply Reply;
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
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL USR:SUB:%s %s", channel, connect_key);

    return 0;
}

std::string caster_internal::get_source_list_text()
{
    return _source_list_text;
}

int caster_internal::set_base_coord_info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, long long update_time)
{
    // 更新坐标到状态信息中
    auto item = _base_status_map.find(connect_key);
    if (item == _base_status_map.end())
    {
        return 1;
    }
    item->second.set_coord_info(ecef_x, ecef_y, ecef_z, update_time);

    // 将ECEF坐标转换成经纬度
    double lat = 0.0, lon = 0.0, alt = 0.0;
    util_ecef2pos(ecef_x, ecef_y, ecef_z, lat, lon, alt);
    // 更新坐标到GEO表中
    redisAsyncCommand(_pub_context, NULL, NULL, "GEOADD MPT:GEO %f %f %s", lon, lat, mount_point); //

    return 0;
}

std::set<std::string> caster_internal::get_active_base_UID()
{
    return _active_baseUID_set;
}

std::set<std::string> caster_internal::get_active_rover_UID()
{
    return _active_roverUID_set;
}

std::string caster_internal::get_active_base_info(std::string UID)
{
    auto iter = _active_base_info_map.find(UID);
    if (iter == _active_base_info_map.end())
    {
        return std::string();
    }
    return iter->second;
}

std::string caster_internal::get_active_rover_info(std::string UID)
{
    auto iter = _active_rover_info_map.find(UID);
    if (iter == _active_rover_info_map.end())
    {
        return std::string();
    }
    return iter->second;
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
            // redisAsyncCommand(_pub_context, NULL, NULL, "HDEL MPT:LIST %s", iter.first.c_str());
        }
    }

    auto register_rover = _rover_register_map; // 创建一个副本
    for (auto iter : register_rover)           // 迭代副本
    {
        if (iter.second.size() == 0)
        {
            _rover_register_map.erase(iter.first);
            // redisAsyncCommand(_pub_context, NULL, NULL, "HDEL USR:LIST %s", iter.first.c_str());
        }
    }

    return 0;
}

int caster_internal::upload_record_item()
{
    for (auto iter : _base_register_map)
    {
        // 更新本地维护的基站
        // 查询
        std::string mount_info_str;
        auto mount_info = _mount_map.find(iter.first);
        if (mount_info != _mount_map.end())
        {
            mount_info_str = convert_mount_info_to_string(mount_info->second);
        }
        else
        {
            mount_info_str = convert_mount_info_to_string(build_default_mount_info(iter.first));
        }
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX MPT:LIST EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter.first.c_str(), mount_info_str.c_str()); // 更新挂载点数据生产者的更新时间

        for (auto items : iter.second)
        {
            // 更新基站注册连接有效期
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE MPT:REC:%s %s FIELDS 1 %s", items.second.channel.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    for (auto iter : _base_sub_map)
    {
        for (auto items : iter.second)
        {
            // 更新本地订阅基站频道的连接信息
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE MPT:SUB:%s %s FIELDS 1 %s", items.second.channel.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    for (auto iter : _rover_register_map)
    {
        // 更新本地维护的用户
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX USR:LIST EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), iter.first.c_str(), _updatetime_str.c_str()); // 更新挂载点数据生产者的更新时间

        for (auto items : iter.second)
        {
            // 更新用户注册连接
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE USR:REC:%s %s FIELDS 1 %s", items.second.user_name.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个用户连接续期
        }
    }

    for (auto iter : _rover_sub_map)
    {
        for (auto items : iter.second)
        {
            // 更新本地订阅用户频道的连接信息
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE USR:SUB:%s %s FIELDS 1 %s", items.second.channel.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    return 0;
}

int caster_internal::download_active_item()
{
    redisAsyncCommand(_pub_context, Redis_Update_Active_Base_Callback, this, "HGETALL MPT:LIST ");
    redisAsyncCommand(_pub_context, Redis_Update_Active_Rover_Callback, this, "HGETALL USR:LIST ");
    return 0;
}

int caster_internal::download_active_info()
{
    if (_download_base_stat)
    {
        redisAsyncCommand(_pub_context, Redis_Update_Base_Info_Callback, this, "HGETALL MPT:STAT ");
    }
    if (_download_rover_stat)
    {
        redisAsyncCommand(_pub_context, Redis_Update_Rover_Info_Callback, this, "HGETALL USR:STAT ");
    }

    return 0;
}

int caster_internal::check_active_base_channel()
{
    auto sub_map = _base_sub_map; // 先复制一份副本,采用副本进行操作，避免执行的回调函数对本体进行了操作，导致for循环出错
    for (auto channel_subs = sub_map.begin(); channel_subs != sub_map.end(); channel_subs++)
    // for (auto channel_subs : _sub_cb_map)
    {
        if (_active_mount_set.find(channel_subs->first) == _active_mount_set.end() && _notify_base_inactive) // 该订阅频道不在活跃频道中
        {
            for (auto item = channel_subs->second.begin(); item != channel_subs->second.end(); item++) // 关闭所有订阅者
            {
                catser_reply Reply;
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
        if (_active_user_set.find(channel_subs->first) == _active_user_set.end() && _notify_rover_inactive) // 该订阅频道不在活跃频道中
        {
            for (auto item = channel_subs->second.begin(); item != channel_subs->second.end(); item++)
            {
                catser_reply Reply;
                Reply.type = CasterReply::ERR;
                Reply.str = "Subscribe Rover is not active";
                auto cb_arg = item->second;
                cb_arg.cb(channel_subs->first.c_str(), cb_arg.arg, &Reply);
            }
        }
        if (channel_subs->second.size() == 0) // 订阅频道的实际用户为0
        {
            _rover_sub_map.erase(channel_subs->first);                                                      // 实际执行的操作是删除了原始记录
            redisAsyncCommand(_sub_context, NULL, NULL, "UNSUBSCRIBE USR:%s", channel_subs->first.c_str()); // 取消订阅该基站频道
        }
    }

    return 0;
}

int caster_internal::build_source_list()
{
    std::string items;
    for (auto iter : _active_mount_set)
    {
        mount_info item;
        auto info = _mount_map.find(iter);
        if (info == _mount_map.end())
        {
            item = build_default_mount_info(iter);
        }
        else
        {
            item = info->second;
        }
        items += convert_mount_info_to_string(item);
    }

    _source_list_text = items;
    return 0;
}

std::string caster_internal::convert_mount_info_to_string(mount_info i)
{
    std::string item;

    item = i.STR + ";" +
           i.mountpoint + ";" +
           i.identufier + ";" +
           i.format + ";" +
           i.format_details + ";" +
           i.carrier + ";" +
           i.nav_system + ";" +
           i.network + ";" +
           i.country + ";" +
           i.latitude + ";" +
           i.longitude + ";" +
           i.nmea + ";" +
           i.solution + ";" +
           i.generator + ";" +
           i.compr_encrryp + ";" +
           i.authentication + ";" +
           i.fee + ";" +
           i.bitrate + ";" +
           i.misc + ";" + "\r\n";

    return item;
}

mount_info caster_internal::build_default_mount_info(std::string mount_point)
{
    // STR;              STR;
    // mountpoint;       KORO996;
    // identufier;       ShangHai;
    // format;           RTCM 3.3;
    // format-details;   1004(5),1074(1),1084(1),1094(1),1124(1)
    // carrier;          2
    // nav-system;       GPS+GLO+GAL+BDS
    // network;          KNT
    // country;          CHN
    // latitude;         36.11
    // longitude;        120.11
    // nmea;             0
    // solution;         0
    // generator;        SN
    // compr-encrryp;    none
    // authentication;   B
    // fee;              N
    // bitrate;          9100
    // misc;             caster.koroyo.xyz:2101/KORO996

    // mount_info item = {
    //     "STR",
    //     mount_point,
    //     "unknown",
    //     "unknown",
    //     "unknown",
    //     "0",
    //     "unknown",
    //     "unknown",
    //     "unknown",
    //     "00.00",
    //     "000.00",
    //     "0",
    //     "0",
    //     "unknown",
    //     "unknown",
    //     "B",
    //     "N",
    //     "0000",
    //     "Not parsed or provided"};

    mount_info item = {
        "STR",
        mount_point,
        "unknown",
        "RTCM 3.3",
        "1074(1),1084(1),1094(1),1124(1)",
        "2",
        "GPS+GLO+GAL+BDS",
        "SNT",
        "XXX",
        "0.00",
        "0.00",
        "1",
        "0",
        "SNT",
        "none",
        "N",
        "N",
        "11520",
        "none"};

    return item;
}

void caster_internal::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<caster_internal *>(arg);
    svr->_updatetime_int = util_get_time_stamp();
    svr->_updatetime_str = util_get_time_stamp_str();

    // 判断ping时间是否已经超过过期时间
    // 如果过期，认为连接已出现未知状况，连接状态置为0，触发重连机制

    // 向redis ping，根据回调确认连接正常

    //  清除本地不再有实际连接注册的基站，这样就不会给这些已经不在线的基站在MPT:LIST中续期
    svr->clear_overdue_item();

    // 本地维护的在线挂载点续期
    // 维护所有的在线挂载点和用户
    // 维护所有的在线记录
    svr->upload_record_item();

    // 获取所有在线挂载点、在线用户列表，删除没有按时续期的用户和挂载点
    svr->download_active_item();

    // 获取所有在线挂载点、在线用户详细信息
    svr->download_active_info();

    // 检测当前活跃的频道(如果本地维护的活跃频道已经不在redis的活跃记录中，那么要把对应的连接踢下线)
    svr->check_active_base_channel();
    svr->check_active_rover_channel();
    // 构建源列表
    svr->build_source_list();
}

int caster_internal::register_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
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
        // 创建一条新的stream记录
        str_status str(channel, channel, 1, user_name, connect_key);
        _base_status_map.insert(std::pair<std::string, str_status>(connect_key, str));
        // 向云端插入记录
        if (_upload_base_stat)
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX MPT:STAT EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, str.get_status_str().c_str());
        }

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
        _active_mount_set.insert(channel); // 同时添加到本地激活列表中去

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性
        // 即：查询到的结果已经包含当前记录，因此避免查询-插入后还需要再进行一步检测的步骤
        // 问题：如果两个节点同时插入了记录，同时查询到记录，按照规则，可能都会被踢掉？
        //       但是踢掉也只是通过广播的形式来踢掉，没啥影响，大不了都登不上，都下线一次
        // 反正记录只能由注册者自己删除（或者说注册该连接的Caster维护）

        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX MPT:REC:%s EX %s FIELDS 1 %s %s",
                          channel,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理

        auto ctx = new std::pair<caster_internal *, caster_cb_item>(this, cb_item);
        redisAsyncCommand(_pub_context, Redis_Register_Base_Callback, ctx, "HGETALL MPT:REC:%s", channel); // 查询当前频道的所有记录
    }
    catch (std::exception &e)
    {
        // 发生异常，此次插入失败
        spdlog::warn("[{}:{}]: exception:  {}", __class__, __func__, e.what());
        return 1;
    }

    return 0;
}

int caster_internal::register_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
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
        // 创建一条新的stream记录
        str_status str(channel, channel, 1, user_name, connect_key);
        _rover_status_map.insert(std::pair<std::string, str_status>(connect_key, str));
        // 向云端插入记录
        if (_upload_rover_stat)
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX USR:STAT EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, str.get_status_str().c_str()); // 更新挂载点数据生产者的更新时间
        }

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
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX USR:REC:%s EX %s FIELDS 1 %s %s",
                          user_name,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理
        auto ctx = new std::pair<caster_internal *, caster_cb_item>(this, cb_item);
        redisAsyncCommand(_pub_context, Redis_Register_Rover_Callback, ctx, "HGETALL USR:REC:%s", user_name); // 查询当前频道的所有记录
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
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL MPT:REC:%s %s", channel, connect_key);

    // 删除status记录
    auto str = _base_status_map.find(connect_key);
    if (str == _base_status_map.end())
    {
        // 错误，找不到这条stream状态记录仪
        return 3;
    }
    _base_status_map.erase(connect_key);
    // 向云端插入记录
    if (_upload_base_stat)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL MPT:STAT %s ", connect_key);
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
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL USR:REC:%s %s", user_name, connect_key);

    // 删除status记录
    auto str = _rover_status_map.find(connect_key);
    if (str == _rover_status_map.end())
    {
        // 错误，找不到这条stream状态记录仪
        return 3;
    }
    _rover_status_map.erase(connect_key);
    // 向云端插入记录
    if (_upload_rover_stat)
    {
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL USR:STAT %s", connect_key);
    }

    return 0;
}

int caster_internal::send_status_base_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason)
{
    // 向redis发布广播
    json req;
    req["Type"] = "BASE";
    req["Ch"] = channel;
    req["Con"] = connect_key;
    req["Status"] = status;
    req["Reason"] = reason;

    std::string msg = req.dump();
    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH CASTER:BROADCAST %s", msg.c_str());
}

int caster_internal::pub_base_channel(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    auto str = _base_status_map.find(connect_key);
    if (str != _base_status_map.end())
    {
        str->second.add_recv(data_length);
        if (_upload_base_stat)
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX MPT:STAT EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, str->second.get_status_str().c_str());
        }
    }
    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH MPT:%s %b", mount_point, data, data_length);
}

int caster_internal::send_status_rover_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason)
{
    // 向redis发布广播
    json req;
    req["Type"] = "ROVER";
    req["Ch"] = channel;
    req["Con"] = connect_key;
    req["Status"] = status;
    req["Reason"] = reason;

    std::string msg = req.dump();
    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH CASTER:BROADCAST %s", msg.c_str());
}

int caster_internal::pub_rover_channel(const char *user_name, const char *connect_key, const char *data, size_t data_length)
{
    auto str = _rover_status_map.find(connect_key);
    if (str != _rover_status_map.end())
    {
        str->second.add_recv(data_length);
        if (_upload_rover_stat)
        {
            redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX USR:STAT EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, str->second.get_status_str().c_str());
        }
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
    redisAsyncCommand(_pub_context, NULL, NULL, "DEL MPT:STAT");
    redisAsyncCommand(_pub_context, NULL, NULL, "DEL USR:STAT");
    return 0;
}

void caster_internal::Redis_Register_Base_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto arg = static_cast<std::pair<caster_internal *, caster_cb_item> *>(privdata);
    auto svr = arg->first;
    auto cb_item = arg->second;
    auto valid_time = svr->_updatetime_int - svr->_unactive_time;

    bool _check = false; // 检验记录中是否包含本条记录

    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::list<std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        if (std::stoll(value) > valid_time) // 更新时间晚于有效时间,该条记录有效
        {
            if (strcmp(field, cb_item.connect_key.c_str()) == 0)
            {
                _check = true;
            }
            records.push_back(field);
        }
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
        catser_reply Reply;
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
    auto valid_time = svr->_updatetime_int - svr->_unactive_time;

    bool _check = false; // 检验记录中是否包含本条记录

    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::list<std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        if (std::stoll(value) > valid_time) // 更新时间晚于有效时间,该条记录有效
        {
            if (strcmp(field, cb_item.connect_key.c_str()) == 0)
            {
                _check = true;
            }
            records.push_back(field);
        }
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
        catser_reply Reply;
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

        catser_reply Reply;
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

            auto str = svr->_rover_status_map.find(cb_item.connect_key);
            if (str != svr->_rover_status_map.end())
            {
                str->second.add_send(Reply.len);
                if (svr->_upload_rover_stat)
                {
                    redisAsyncCommand(svr->_pub_context, NULL, NULL, "HSETEX USR:STAT EX %s FIELDS 1 %s %s", std::to_string(svr->_key_expire_time).c_str(), cb_item.connect_key.c_str(), str->second.get_status_str().c_str());
                }
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

        catser_reply Reply;
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

            auto str = svr->_base_status_map.find(cb_item.connect_key);
            if (str != svr->_base_status_map.end())
            {
                str->second.add_send(Reply.len);
                if (svr->_upload_base_stat)
                {
                    redisAsyncCommand(svr->_pub_context, NULL, NULL, "HSETEX MPT:STAT EX %s FIELDS 1 %s %s", std::to_string(svr->_key_expire_time).c_str(), cb_item.connect_key.c_str(), str->second.get_status_str().c_str());
                }
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

int caster_internal::subAttemptReconnect()
{
    if (_is_sub_connected)
    {
        return 0;
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
        _pub_context = nullptr;
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

void caster_internal::Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 订阅到的是一个Json字符串

    // 字符串组成：
    //   对象type  Base/Rover
    //   频道ch    MountName/UserName        "MPT"
    //   连接con   Connect_Key       "XXXXXXXXXXXX"
    //   指令Status   Status              CASTER_RELPY_
    //   原因reason  reason            "Already Online / New Same Name Login"

    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    json req;
    std::string type, ch, con, reason;
    CasterReply status;

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
    try
    {
        req = json::parse(re3->str);
        type = req["Type"];
        ch = req["Ch"];
        con = req["Con"];
        status = req["Status"];
        reason = req["Reason"];
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[{}:{}]: exception:  {}", __class__, __func__, e.what());
        return;
    }

    auto sub_map = (type == "BASE") ? &svr->_base_register_map : &svr->_rover_register_map;

    auto item = sub_map->find(ch);
    if (item == sub_map->end())
    {
        return; // 本地没有该频道的注册记录
    }

    // 复制字符串
    catser_reply Reply;
    Reply.type = status;
    Reply.str = reason.c_str();

    if (con.size() == 0) // 没有指定特定的连接，则对所有的连接都发送一次回复（针对允许同名频道都在线的情况）
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
        auto target = item->second.find(con);
        if (target == item->second.end())
        {
            return; // 本地没有该连接的注册记录
        }

        auto cb_item = target->second;
        auto Func = cb_item.cb;
        auto arg = cb_item.arg;
        Func(NULL, arg, &Reply);
    }
    return;
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

    svr->_active_mount_set.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        auto valid_time = svr->_updatetime_int - svr->_unactive_time;

        svr->_active_mount_set.insert(field);
    }

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

    svr->_active_user_set.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        auto valid_time = svr->_updatetime_int - svr->_unactive_time;

        svr->_active_user_set.insert(field);
    }
    // spdlog::info("Sync active rover, current item:{} ", svr->_active_user_set.size());
}

void caster_internal::Redis_Update_Base_Info_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL MPT:STAT: reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL MPT:STAT reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    // if (reply->elements == 0)
    // {
    //     spdlog::info("[{}:{}]: HGETALL MPT:STAT reply->elements: {}", __class__, __func__, reply->elements);
    //     return;
    // }

    svr->_active_baseUID_set.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        svr->_active_baseUID_set.insert(field);
        svr->_active_base_info_map.insert(std::pair<std::string, std::string>(field, value));
    }
}

void caster_internal::Redis_Update_Rover_Info_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<caster_internal *>(privdata);

    if (!reply)
    {
        return;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        spdlog::warn("[{}:{}]: HGETALL USR:STAT: reply->type == REDIS_REPLY_NIL", __class__, __func__);
        return;
    }
    if (reply->type != REDIS_REPLY_ARRAY)
    {
        spdlog::error("[{}:{}]: HGETALL USR:STAT reply->type != REDIS_REPLY_ARRAY: {}", __class__, __func__, reply->type);
        return;
    }

    // if (reply->elements == 0)
    // {
    //     spdlog::info("[{}:{}]: HGETALL USR:STAT reply->elements: {}", __class__, __func__, reply->elements);
    //     return;
    // }

    svr->_active_roverUID_set.clear();

    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;
        svr->_active_roverUID_set.insert(field);
        svr->_active_rover_info_map.insert(std::pair<std::string, std::string>(field, value));
    }
}

// 将十六进制字符串解析为十进制整数
int hexToDec(const std::string &hexStr)
{
    int value;
    std::stringstream ss;
    ss << std::hex << hexStr;
    ss >> value;
    return value;
}

// 从16进制字符串还原IP和端口
void decodeKey(const std::string &key, std::string &serverIP, int &serverPort, std::string &clientIP, int &clientPort)
{
    if (key.size() != 24)
    {
        throw std::invalid_argument("Invalid key length");
    }

    // 分离16进制字符串
    std::string hexIp1 = key.substr(0, 8);    // 服务器IP部分
    std::string hexPort1 = key.substr(8, 4);  // 服务器端口部分
    std::string hexIp2 = key.substr(12, 8);   // 客户端IP部分
    std::string hexPort2 = key.substr(20, 4); // 客户端端口部分

    // 解析服务器IP
    serverIP = std::to_string(hexToDec(hexIp1.substr(0, 2))) + "." +
               std::to_string(hexToDec(hexIp1.substr(2, 2))) + "." +
               std::to_string(hexToDec(hexIp1.substr(4, 2))) + "." +
               std::to_string(hexToDec(hexIp1.substr(6, 2)));

    // 解析服务器端口
    serverPort = hexToDec(hexPort1);

    // 解析客户端IP
    clientIP = std::to_string(hexToDec(hexIp2.substr(0, 2))) + "." +
               std::to_string(hexToDec(hexIp2.substr(2, 2))) + "." +
               std::to_string(hexToDec(hexIp2.substr(4, 2))) + "." +
               std::to_string(hexToDec(hexIp2.substr(6, 2)));

    // 解析客户端端口
    clientPort = hexToDec(hexPort2);
}

str_status::str_status(std::string login_mpt, std::string alias_mpt, int type, std::string user_name, std::string connect_key)
{
    _UID = connect_key;
    _login_mpt = login_mpt;
    _alias_mpt = alias_mpt;
    _type = type;
    _account = user_name;

    std::string server_ip;
    int server_port;
    decodeKey(connect_key, server_ip, server_port, _ip, _port);

    _send_total = 0; // 总发送字节数
    _send_count = 0;
    _send_speed = 0; // 总发送速度

    _recv_total = 0; // 总接收字节数
    _recv_count = 0;
    _recv_speed = 0; // 总接收速度

    // 转换为 time_t 类型，表示从1970-01-01 00:00:00 UTC开始的秒数
    _update_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    _online_time = _update_time; // 登录时间设置为提前1秒，保证_online_seconds能够不为0
    _online_seconds = _update_time - _online_time;
}

str_status::~str_status()
{
}

int str_status::add_recv(int size)
{
    _recv_total += size;
    _update_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto sec = _update_time - _online_time;
    if (sec - _online_seconds < 5)
    {
        _recv_count += size;
    }
    else
    {
        update_speed();
    }

    return 0;
}

int str_status::add_send(int size)
{
    _send_total += size;
    _update_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto sec = _update_time - _online_time;
    if (sec - _online_seconds < 5)
    {
        _send_count += size;
    }
    else
    {
        update_speed();
    }
    return 0;
}

int str_status::set_alias_mpt(std::string alias_mpt)
{
    _alias_mpt = alias_mpt;
    return 0;
}

int str_status::set_coord_info(double ecef_x, double ecef_y, double ecef_z, long long update_time)
{
    _ecef_x = ecef_x;
    _ecef_y = ecef_y;
    _ecef_z = ecef_z;
    _position_update_time = update_time;
    return 0;
}

int str_status::update_speed()
{
    _update_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto sec = _update_time - _online_time;

    _recv_speed = _recv_count / (sec - _online_seconds);
    _send_speed = _send_count / (sec - _online_seconds);

    _send_count = 0;
    _send_count = 0;
    _online_seconds = sec;

    return 0;
}

std::string str_status::get_status_str()
{
    json info;

    info["UID"] = _UID;
    info["login_mpt"] = _login_mpt;
    info["alias_mpt"] = _alias_mpt;
    info["type"] = _type;
    info["account"] = _account;
    info["ip"] = _ip;
    info["port"] = _port;
    info["online_time"] = _online_time;
    info["online_seconds"] = _online_seconds;

    info["send_total"] = _send_total;
    info["send_speed"] = _send_speed;
    info["recv_total"] = _recv_total;
    info["recv_speed"] = _recv_speed;

    info["ecef_x"] = _ecef_x;
    info["ecef_y"] = _ecef_y;
    info["ecef_z"] = _ecef_z;
    info["position_update_time"] = _position_update_time;

    info["update_time"] = _update_time;

    return info.dump();
}
