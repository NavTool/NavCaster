#pragma once
#include <event2/event.h>
#include <string>

// #define CASTER_REPLY_ERR -1
// #define CASTER_REPLY_OK 0
// #define CASTER_REPLY_ACTIVE 5   // 激活，当前有用户订阅该频道（针对注册的回调）
// #define CASTER_REPLY_INACTIVE 6 // 闲置，没有用户订阅该频道  （针对注册的回调）

// #define CASTER_REPLY_STRING 1
// #define CASTER_REPLY_ARRAY 2
// #define CASTER_REPLY_INTEGER 3
// #define CASTER_REPLY_NIL 4


enum class CasterReply
{
    ERR = -1,
    OK,
    ACTIVE,
    INACTIVE,
    STRING,
    // ARRAY,
    INTEGER,
    NIL,
};

struct catser_reply
{
    CasterReply type;
    const char *str;
    size_t len;
    int integer;
};

struct mount_info
{
    std::string STR;
    std::string mountpoint;
    std::string identufier;
    std::string format;
    std::string format_details;
    std::string carrier;
    std::string nav_system;
    std::string network;
    std::string country;
    std::string latitude;
    std::string longitude;
    std::string nmea;
    std::string solution;
    std::string generator;
    std::string compr_encrryp;
    std::string authentication;
    std::string fee;
    std::string bitrate;
    std::string misc;
};

typedef void (*CasterCallback)(const char *request, void *arg, catser_reply *reply);

namespace CASTER
{
    // 基础函数
    int Init(const char *json_conf, event_base *base);
    int Free();

    std::string Get_Status();

    // 服务用函数---------------------------------------------------------------------------------------------------------

    // 将基站注册到Caster中（Server上线的时候主动调用）
    int Register_Base_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 将基站从Caster中注销（Server下线的时候主动调用）
    int Withdraw_Base_Record(const char *mount_point, const char *user_name, const char *connect_key);
    // 发布基站数据
    int Pub_Base_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length);
    // 订阅基站数据
    int Sub_Base_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅基站数据
    int Unsub_Base_Raw_Data(const char *mount_point, const char *connect_key);
    // 获取订阅基站的用户信息
    int Get_Sub_Base_Count(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg);

    // // 检查基站是否在线（回复在线数量）
    // int Check_Base_Online(const char *mount_point, CasterCallback cb, void *arg);
    // // 获取指定基站在线记录
    // int Get_Base_Record(const char *mount_point, CasterCallback cb, void *arg);

    // 将移动站注册到Caster中（Client上线的时候主动调用）
    int Register_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 将移动站从Caster中注销（Client下线的时候主动调用）
    int Withdraw_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key);
    // 发布移动站数据
    int Pub_Rover_Raw_Data(const char *user_name, const char *connect_key, const char *data, size_t data_length);
    // 订阅移动站数据
    int Sub_Rover_Raw_Data(const char *mount_point,const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅移动站数据
    int Unsub_Rover_Raw_Data(const char *user_name, const char *connect_key);
    // 获取订阅移动站的用户信息
    int Get_Sub_Rover_Count(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg);

    // 获取文本形式的源列表
    std::string Get_Source_Table_Text();

    // 信息上报函数()   挂载点名，挂载点的connect_key,挂载点描述（上线时刻，上线时长，接收数据统计、发送数据统计、发送数据速度，接收数据速度）
    int Update_Base_Describe(const char *mount_point, const char *connect_key, const char *describe);
    // 信息上报函数()   用户名，用户的connect_key,用户描述（上线时刻，上线时长，接收数据统计、发送数据统计、发送数据速度，接收数据速度）
    int Update_Rover_Describe(const char *user_name, const char *connect_key, const char *describe);

    // 管理用函数---------------------------------------------------------------------------------------------------------

    //  主动停止指定的基站
    int Stop_One_Base(const char *mount_point, const char *connect_key, const char *reason);
    // 主动停止指定的移动站
    int Stop_One_Rover(const char *user_name, const char *connect_key, const char *reason);

    // 最近点基站模式------------------------------------------------------------------------------------------------------
    int Sub_Base_Raw_Data(double lat, double lon, const char *connect_key, CasterCallback cb, void *arg);

    // 更新基站源列表信息(上报源列表，如果Caster_Core允许半径筛选模式，则同步更新源列表坐标到GEO表中，GEO表中的坐标采用刷新模式？)
    int Set_Base_Source_Info(const char *mount_point, const char *connect_key, mount_info);

    // 更新用户位置信息，将用户的信息上报到Caster_Core

    // Cors模式 ----------------------------------------------------------------------------------------------------------

    // 注册虚拟基站
    int Register_Grid_Record(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg);
    // 取消注册虚拟基站
    int Withdraw_Grid_Record(const char *mount_point, const char *connect_key);
    // 发布虚拟基站数据
    int Pub_Grid_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length);
    // 订阅虚拟基站数据
    int Sub_Grid_Raw_Data(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg);
    // 订阅最近的虚拟参考站（回调函数中应返回实际订阅的基站是哪个）
    int Sub_Grid_Raw_Data(double lat, double lon, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅虚拟参考站
    int Unsub_Grid_Raw_Data(const char *mount_point, const char *connect_key);

    // 设置虚拟基站的信息
    int Set_Grid_Source_Info(const char *mount_point, const char *connect_key, mount_info);




}

// Redis 内部维护表
// 一张
