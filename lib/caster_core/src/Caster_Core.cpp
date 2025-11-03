#include "Caster_Core.h"
#include <spdlog/spdlog.h>

// #include "caster_core_internal.h"
#include "caster_internal.h"

// redis_msg_internal *caster_svr = nullptr;

caster_internal *caster = nullptr;

// int CASTER::Init(const char *json_conf, event_base *base)
// {
//     json conf = json::parse(json_conf);

//     if (caster_svr == nullptr)
//     {
//         caster_svr = new redis_msg_internal(conf, base);
//         caster_svr->start();
//     }
//     return 0;
// }

// int CASTER::Free()
// {
//     caster_svr->stop();
//     delete caster_svr;
//     return 0;
// }

// int CASTER::Clear()
// {
//     // 清除与指定在线表（单实例部署）
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "DEL MOUNT:ONLINE:COMMON");
//     return 0;
// }

// int CASTER::Clear(const char *server_key)
// {
//     // 清除与指定server_key(服务端口）相关的所有连接（暂未实现（用于集群部署）
//     return 0;
// }

// int CASTER::Check_Mount_Type(const char *mount_point)
// {
//     return CASTER::STATION_COMMON;
// }

// int CASTER::Set_Base_Station_State_ONLINE(const char *mount_point, const char *user_name, const char *connect_key, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "HSET MOUNT:ONLINE:COMMON %s %s", mount_point, connect_key); // 改成   挂载点-时间的格式
//     redisAsyncCommand(context, NULL, NULL, "HSET CHANNEL:ACTIVE MOUNT:%s %s", mount_point, connect_key);
//     caster_svr->add_local_active_connect_key(connect_key);

//     // 添加到本地的挂载点列表中（mount-connect_key)
//     // 向云端添加一条在线记录        HSET MOUNT:ONLINE:COMMON         挂载点：当前时间
//     // 向云端添加活跃频道记录        HSET CHANNEL:ACTIVE              挂载点：Connect_key
//     // 向云端添加订阅该频道的记录     HSET CHANNEL:SUBS:挂载点         Connect_Key:name

//     return 0;
// }

// int CASTER::Set_Base_Station_State_OFFLINE(const char *mount_point, const char *user_name, const char *connect_key, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "HDEL MOUNT:ONLINE:COMMON %s", mount_point);
//     redisAsyncCommand(context, NULL, NULL, "HDEL CHANNEL:ACTIVE MOUNT:%s", mount_point);
//     caster_svr->del_local_active_connect_key(connect_key);

//     return 0;
// }

// int CASTER::Check_Base_Station_is_ONLINE(const char *mount_point, CasterCallback cb, void *arg, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     auto ctx = new caster_cb_item();
//     ctx->cb = cb;
//     ctx->arg = arg;
//     redisAsyncCommand(context, redis_msg_internal::Redis_ONCE_Callback, ctx, "HEXISTS MOUNT:ONLINE:COMMON %s", mount_point);
//     return 0;
// }

// int CASTER::Pub_Base_Station_Raw_Data(const char *mount_point, const char *data, size_t data_length, const char *connect_key, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     redisAsyncCommand(context, NULL, NULL, "PUBLISH %s %b", channel.c_str(), data, data_length);
//     return 0;
// }

// int CASTER::Get_Base_Station_Sub_Num(const char *mount_point, CasterCallback cb, void *arg, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     auto ctx = new caster_cb_item();
//     ctx->cb = cb;
//     ctx->arg = arg;
//     redisAsyncCommand(context, redis_msg_internal::Redis_ONCE_Callback, ctx, "SCARD CHANNEL:%s:SUBS", channel.c_str());
//     return 0;
// }

// int CASTER::Sub_Base_Station_Raw_Data(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg, Station_type type)
// {
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     caster_svr->add_sub_cb_item(channel.c_str(), connect_key, cb, arg);
//     return 0;
// }

// int CASTER::UnSub_Base_Station_Raw_Data(const char *mount_point, const char *connect_key, Station_type type)
// {
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     caster_svr->del_sub_cb_item(channel.c_str(), connect_key);
//     return 0;
// }

// int CASTER::Set_Rover_Client_State_ONLINE(const char *mount_point, const char *user_name, const char *connect_key, Client_type type)
// {
//     // auto context = caster_svr->_pub_context;
//     // redisAsyncCommand(context, NULL, NULL, "HSET CLIENT:ONLINE:COMMON %s %s", mount_point, connect_key);
//     return 0;
// }

// int CASTER::Set_Rover_Client_State_OFFLINE(const char *mount_point, const char *user_name, const char *connect_key, Client_type type)
// {
//     return 0;
// }

// int CASTER::Check_Rover_Client_is_ONLINE(const char *user_name, CasterCallback cb, void *arg, Client_type type)
// {
//     return 0;
// }

// int CASTER::Pub_Rover_Client_Raw_Data(const char *client_key, const char *data, size_t data_length, const char *connect_key, Client_type type)
// {
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "PUBLISH CLIENT:%s %b", client_key, data, data_length);
//     return 0;
// }

// int CASTER::Sub_Rover_Client_Raw_Data(const char *client_key, CasterCallback cb, void *arg, const char *connect_key, Client_type type)
// {
//     std::string channel;
//     channel += "CLIENT:";
//     channel += client_key;
//     caster_svr->add_sub_cb_item(channel.c_str(), connect_key, cb, arg);
//     return 0;
// }

// int CASTER::Get_Rover_Client_Sub_Num(const char *mount_point, CasterCallback cb, void *arg, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     std::string channel;
//     channel += "CLIENT:";
//     channel += mount_point;
//     auto ctx = new caster_cb_item();
//     ctx->cb = cb;
//     ctx->arg = arg;
//     redisAsyncCommand(context, redis_msg_internal::Redis_ONCE_Callback, ctx, "SCARD CHANNEL:%s:SUBS", channel.c_str());
//     return 0;
// }

// int CASTER::UnSub_Rover_Client_Raw_Data(const char *client_key, const char *connect_key, Client_type type)
// {
//     std::string channel;
//     channel += "CLIENT:";
//     channel += client_key;
//     caster_svr->del_sub_cb_item(channel.c_str(), connect_key);
//     return 0;
// }

// int CASTER::Get_Source_Table_List(CasterCallback cb, void *arg, Source_type type)
// {
//     return 0;
// }

// int CASTER::Add_Source_Table_Item(const char *mount_point, const char *info, double lon, double lat, Source_type type)
// {
//     return 0;
// }

// int CASTER::Del_Source_Table_Item(const char *mount_point, Source_type type)
// {
//     return 0;
// }

// int CASTER::Get_Source_Table_Item(const char *mount_point, CasterCallback cb, void *arg, Source_type type)
// {
//     return 0;
// }

// int CASTER::Get_Radius_Table_List(double lon, double lat, CasterCallback cb, void *arg, Source_type type)
// {
//     return 0;
// }

// std::string CASTER::Get_Source_Table_Text()
// {
//     return caster_svr->get_source_list_text();
// }

int CASTER::Init(const char *json_conf, event_base *base)
{
    json conf = json::parse(json_conf);

    if (caster == nullptr)
    {
        caster = new caster_internal(conf, base);
        caster->start();
    }
    return 0;
}

int CASTER::Free()
{
    caster->stop();
    delete caster;
    return 0;
}

std::string CASTER::Get_Status()
{
    return caster->get_status_str();
}

int CASTER::Register_Base_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
{
    return caster->register_base_channel(mount_point, user_name, connect_key, cb, arg);
}

int CASTER::Withdraw_Base_Record(const char *mount_point, const char *user_name, const char *connect_key)
{
    return caster->withdraw_base_channel(mount_point, user_name, connect_key);
}

int CASTER::Pub_Base_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    return caster->pub_base_channel(mount_point, connect_key, data, data_length);
}

int CASTER::Sub_Base_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
{
    return caster->sub_base_channel(mount_point, user_name, connect_key, cb, arg);
}

int CASTER::Unsub_Base_Raw_Data(const char *mount_point, const char *connect_key)
{
    return caster->unsub_base_channel(mount_point, connect_key);
}

int CASTER::Stop_One_Base(const char *mount_point, const char *connect_key, const char *reason)
{
    return caster->send_status_base_channel(mount_point, connect_key, CasterReply::ERR, reason);
}

// int CASTER::Check_Base_Online(const char *mount_point, CasterCallback cb, void *arg)
// {
//     return 0;
// }

// int CASTER::Get_Base_Record(const char *mount_point, CasterCallback cb, void *arg)
// {
//     return 0;
// }

int CASTER::Register_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
{
    return caster->register_rover_channel(mount_point, user_name, connect_key, cb, arg);
}

int CASTER::Withdraw_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key)
{
    return caster->withdraw_rover_channel(mount_point, user_name, connect_key);
}

int CASTER::Pub_Rover_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    return caster->pub_rover_channel(mount_point, connect_key, data, data_length);
}

int CASTER::Sub_Rover_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg)
{
    return caster->sub_rover_channel(mount_point, user_name, connect_key, cb, arg);
}

int CASTER::Unsub_Rover_Raw_Data(const char *user_name, const char *connect_key)
{
    return caster->unsub_rover_channel(user_name, connect_key);
}

int CASTER::Get_Sub_Rover_Count(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Get_Sub_Base_Count(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Stop_One_Rover(const char *user_name, const char *connect_key, const char *reason)
{
    return caster->send_status_rover_channel(user_name, connect_key, CasterReply::ERR, reason);
}

int CASTER::Sub_Base_Raw_Data(double lat, double lon, const char *connect_key, CasterCallback cb, void *arg)
{

    // 添加一个查询，查询最近的站点

    // 查找成功，

    // 查找所有在半径范围内的点的在线状态

    // 返回状态，订阅距离最近且在线状态正常的点

    // 查询当前connect_key是否已经有订阅站点，

    // 如果已经有订阅，取消原先的订阅，添加到新的订阅点，触发更改订阅成功

    return 0;
}

int CASTER::Set_Base_Source_Info(const char *mount_point, const char *connect_key, mount_info)
{
    // 自动上报的消息，更新的mount_point的挂载点显示信息
    // 同时写入到GEO表中 MPT:INFO:GEO

    // MPT:

    return 0;
}

int CASTER::Register_Grid_Record(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Withdraw_Grid_Record(const char *mount_point, const char *connect_key)
{
    return 0;
}

int CASTER::Pub_Grid_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    return 0;
}

int CASTER::Sub_Grid_Raw_Data(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Sub_Grid_Raw_Data(double lat, double lon, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Unsub_Grid_Raw_Data(const char *mount_point, const char *connect_key)
{
    return 0;
}

int CASTER::Set_Grid_Source_Info(const char *mount_point, const char *connect_key, mount_info)
{
    return 0;
}

std::string CASTER::Get_Source_Table_Text()
{
    return caster->get_source_list_text();
}

int CASTER::Update_Base_Describe(const char *mount_point, const char *connect_key, const char *describe)
{
    return 0;
}

int CASTER::Update_Rover_Describe(const char *user_name, const char *connect_key, const char *describe)
{
    return 0;
}
