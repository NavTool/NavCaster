#pragma once
#include "context_util.h"

// 移动站连接状态
class client_status
{
private:
    std::string _uid;         // connect_key
    std::time_t _online_time; // 上线时刻
    std::time_t _update_time; // 更新时刻

    std::string _login_mpt; // 接入的挂载点
    std::string _alias_mpt; // 对外服务的名称
    int _type;              // 接入类型
    std::string _account;   // 账户名
    std::string _ip;        // 连接IP
    int _port;              // 连接端口


    double _ecef_x = 0;
    double _ecef_y = 0;
    double _ecef_z = 0;
    std::time_t _position_update_time = 0;

    int _quality = 0;    // 定位状态
    int _sat_num = 0;    // 卫星数
    double _diff = 0;    // 差分延迟
    double _distance = 0; // 距离

public:
    client_status(std::string uid)
    {
        _uid=uid;
    }

    int set_alias_mpt(std::string alias_mpt)
    {
        _alias_mpt=alias_mpt;
        return 0;
    }

    int set_coord_info(double ecef_x,double ecef_y,double ecef_z,int Q,int sat,double diff)
    {
        _ecef_x=ecef_x;
        _ecef_y=ecef_y;
        _ecef_z=ecef_z;
        _quality=Q;
        _sat_num=sat;
        _diff=diff;

        _position_update_time=util_get_now_second();
        return 0;
    }

    int set_distance(double distance)
    {
        _distance=distance;
        return 0;
    }

    int fromString(const std::string &str)
    {
        return 0;
    }
    std::string toString()
    {
        // 创建一个proto
        caster::core::ClientState proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
