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

enum class CasterRelayType
{
    UNSPECIFIED = 0,
    SERVER_OPERATE = 1,
    CLIENT_OPERATE = 2,
    PUSH_OPERATE = 3,
    PULL_OPERATE = 4,
};

enum class CasterRelayOperate
{
    UNSPECIFIED = 0,
    CREATE = 1,
    UPDATE = 2,
    DELETE = 3,
    ACTIVE = 4,
    INACTIVE = 5,
};

struct CasterRelayMsg
{
    CasterRelayType type = CasterRelayType::UNSPECIFIED;
    CasterRelayOperate operate = CasterRelayOperate::UNSPECIFIED;
    std::string target;
    std::string msg_str;
    std::string reason_str;
};

typedef void (*RelayCallback)(void *arg, const CasterRelayMsg &msg);

namespace CASTER
{
    // 基础函数
    int Init(CasterCoreOpt opt, event_base *base);
    int Free();

    std::string Get_Status();

    // 服务用函数---------------------------------------------------------------------------------------------------------

    // 检测是否是最近挂载点模式
    bool Check_Nearest_Mpt(const char *mount_point);

    bool Check_Alias_Mpt(const char *mount_point);

    // 注册连接记录（按 CasterRegisterType 区分基站/移动站/Pull/Push/Nearest/Alias）
    int Register_Record(const char *connect_key, const char *mount_point, const char *user_name, CasterCallback cb, void *arg, CasterRegisterType type);
    // 注销连接记录
    int Withdraw_Record(const char *connect_key, const char *mount_point, const char *user_name, CasterRegisterType type);

    // 发布数据
    int Pub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, const char *data, size_t data_length, CasterRegisterType type);
    // 订阅数据（NEAREST 模式时需要传入 lat/lon）
    int Sub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, CasterCallback cb, void *arg, CasterRegisterType type, double lat = 0.0, double lon = 0.0);
    // 取消订阅数据
    int Unsub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, CasterRegisterType type);

    // 设置连接延迟信息
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
