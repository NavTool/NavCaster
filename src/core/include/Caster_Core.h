#pragma once
#include <event2/event.h>
#include <list>
#include <string>
#include <set>

#include "service/CasterCoreOpt.pb.h"
using namespace caster::service;

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
    DOUBLE,
    NIL,
};

struct caster_reply
{
    CasterReply type;
    const char *str;
    size_t len;
    int integer = 0;
    double dval = 0.0;
};

enum class CasterRegisterType
{
    UNKNOWN = 0,
    SERVER = 1, // 基站模式
    CLIENT,     // 移动站模式
    NEAREST,    // 最近挂载点模式
    ALIAS,      // 别名挂载点模式
    PULL,       // 拉取模式
    PUSH        // 推送模式
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

typedef void (*CasterCallback)(const char *request, void *arg, caster_reply *reply);

class broadcast_msg;
typedef void (*RelayCallback)(void *arg, const broadcast_msg &msg);

namespace CASTER
{
    // 基础函数
    int Init(CasterCoreOpt opt, event_base *base);
    int Free();
    void Set_Node_Runtime_Info(uint32_t listen_port, uint32_t http_port, uint32_t process_id);
    bool Is_Master_Node();

    // 返回当前节点 ID (例如 host_listenPort_httpPort_pid)
    std::string Get_Node_ID();

    std::string Get_Status();

    // 服务用函数---------------------------------------------------------------------------------------------------------

    // 检测是否是最近挂载点模式
    bool Check_Nearest_Mpt(const char *mount_point);

    bool Check_Alias_Mpt(const char *mount_point);

    int Register_Record(const char *connect_key, const char *mount_point, const char *user_name, CasterCallback cb, void *arg, CasterRegisterType type);
    int Withdraw_Record(const char *connect_key, const char *mount_point, const char *user_name, CasterRegisterType type);

    int Pub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, const char *data, size_t data_length, CasterRegisterType type);
    int Sub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, CasterCallback cb, void *arg, CasterRegisterType type);
    int Unsub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, CasterRegisterType type);

    // 将基站注册到Caster中（Server上线的时候主动调用）
    int Register_Base_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type);
    // 将基站从Caster中注销（Server下线的时候主动调用）
    int Withdraw_Base_Record(const char *mount_point, const char *user_name, const char *connect_key);
    // 发布基站数据
    int Pub_Base_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length);
    // 订阅基站数据
    int Sub_Base_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 最近点基站模式
    int Sub_Near_Raw_Data(const char *mount_point, double lat, double lon, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消最近点基站模式订阅
    int Unsub_Near_Raw_Data(const char *connect_key);
    // 订阅基站数据
    int Sub_Alias_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅基站数据
    int Unsub_Base_Raw_Data(const char *mount_point, const char *connect_key);
    // 设置基站坐标信息
    int Set_Base_Coord_Info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z);
    // 设置基站连接延迟信息
    int Set_Pull_Base_Info(const char *task_key, const char *alias_mpt, const char *connect_key, int state);

    int Set_Push_Rover_Info(const char *task_key, const char *alias_mpt, const char *connect_key, int state);

    // 更新基站源列表信息(上报源列表，如果Caster_Core允许半径筛选模式，则同步更新源列表坐标到GEO表中，GEO表中的坐标采用刷新模式？)
    int Set_Base_Source_Info(const char *mount_point, const char *connect_key, mount_info);

    // 更新基站源列表解析信息(根据RTCM数据流自动解析出的报文类型和卫星系统)
    int Set_Base_Source_Info(const char *mount_point, const char *connect_key, const std::string &format_details, const std::string &nav_system);

    // 将移动站注册到Caster中（Client上线的时候主动调用）
    int Register_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type);
    // 将移动站从Caster中注销（Client下线的时候主动调用）
    int Withdraw_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key);
    // 发布移动站数据
    int Pub_Rover_Raw_Data(const char *user_name, const char *connect_key, const char *data, size_t data_length);
    // 订阅移动站数据
    int Sub_Rover_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅移动站数据
    int Unsub_Rover_Raw_Data(const char *user_name, const char *connect_key);
    // 设置用户坐标信息
    int Set_Rover_Coord_Info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, int Q, int sat, double diff);
    // 设置用户连接延迟信息
    int Set_Delay_Info(const char *connect_key, uint64_t delay);

    // 获取文本形式的源列表
    std::string Get_Source_Table_Text();

    int Relay_Register_Callback(RelayCallback cb, void *arg);

    // 管理用函数---------------------------------------------------------------------------------------------------------

    // //  主动停止指定的基站
    // int Stop_One_Base(const char *mount_point, const char *connect_key, const char *reason);
    // // 主动停止指定的移动站
    // int Stop_One_Rover(const char *user_name, const char *connect_key, const char *reason);

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
