#pragma once
#include "context_util.h"

// 移动站连接状态
class client_status
{
private:
    std::string _uid;              // connect_key
    std::time_t _online_time = 0;  // 上线时刻
    std::time_t _update_time = 0;  // 更新时刻

    std::string _login_mpt; // 接入的挂载点
    std::string _alias_mpt; // 对外服务的名称
    int _type = 0;           // 接入类型
    std::string _account;   // 账户名
    std::string _ip;        // 连接IP
    int _port = 0;           // 连接端口


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
        _online_time = util_get_now_second();
    }

    int set_info(std::string login_mpt, int type, std::string account)
    {
        _login_mpt = login_mpt;
        _alias_mpt = _login_mpt;
        _type = type;
        _account = account;

        std::string server_ip;
        int server_port;
        decodeKey(_uid, server_ip, server_port, _ip, _port);

        _update_time = util_get_now_second();
        return 0;
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
        caster::core::ClientState proto;
        if (!JsonToProto(str, proto))
            return 1;
        _uid = proto.uid();
        _online_time = proto.online_time();
        _update_time = proto.update_time();
        _login_mpt = proto.login_mpt();
        _alias_mpt = proto.alias_mpt();
        _type = proto.type();
        _account = proto.account();
        _ip = proto.ip();
        _port = proto.port();
        _ecef_x = proto.ecef_x();
        _ecef_y = proto.ecef_y();
        _ecef_z = proto.ecef_z();
        _position_update_time = proto.position_update_time();
        _quality = proto.quality();
        _sat_num = proto.sat_num();
        _diff = proto.diff();
        _distance = proto.distance();
        return 0;
    }
    std::string toString()
    {
        _update_time = util_get_now_second();

        caster::core::ClientState proto;
        proto.set_uid(_uid);
        proto.set_online_time(static_cast<uint64_t>(_online_time));
        proto.set_update_time(static_cast<uint64_t>(_update_time));
        proto.set_login_mpt(_login_mpt);
        proto.set_alias_mpt(_alias_mpt);
        proto.set_type(_type);
        proto.set_account(_account);
        proto.set_ip(_ip);
        proto.set_port(_port);
        proto.set_online_seconds(static_cast<uint64_t>(_online_time > 0 ? _update_time - _online_time : 0));
        proto.set_ecef_x(_ecef_x);
        proto.set_ecef_y(_ecef_y);
        proto.set_ecef_z(_ecef_z);
        proto.set_position_update_time(static_cast<int64_t>(_position_update_time));
        proto.set_quality(_quality);
        proto.set_sat_num(_sat_num);
        proto.set_diff(_diff);
        proto.set_distance(_distance);
        return ProtoToJson(proto);
    }
};
