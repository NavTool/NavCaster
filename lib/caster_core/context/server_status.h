#pragma once
#include "context_util.h"

// 基站连接状态
class server_status
{
public:
    std::string _uid;    // connect_key
    std::time_t _online_time; // 上线时刻
    std::time_t _update_time; // 更新时刻

    std::string _login_mpt; // 接入的挂载点
    std::string _alias_mpt; // 对外服务的名称
    int _type;              // 挂载点类型
    std::string _account;   // 账户名
    std::string _ip;        // 连接IP    // 通过connect_key解析
    int _port;              // 连接端口

    double _ecef_x = 0;
    double _ecef_y = 0;
    double _ecef_z = 0;
    std::time_t _position_update_time = 0;   // 定时上报



public:
    server_status(std::string uid)
    {
        _uid=uid;
    }

    int set_alias_mpt(std::string alias_mpt)
    {
        _alias_mpt=alias_mpt;
        return 0;
    }
    int set_coord_info(double ecef_x,double exef_y,double ecef_z)
    {
        _ecef_x=ecef_x;
        _ecef_y=exef_y;
        _ecef_z=ecef_z;
        _position_update_time=util_get_now_second();
        return 0;
    }

    int fromString(const std::string &str)
    {
        return 0;
    }
    std::string toString()
    {
        // 创建一个proto
        caster::core::ServerState proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
