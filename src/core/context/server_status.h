#pragma once
#include "context_util.h"

// 基站连接状态
class server_status
{
public:
    std::string _uid;         // connect_key
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
    std::time_t _position_update_time = 0; // 定时上报

    // 从RTCM解析出的源列表信息
    std::string _format_details;  // RTCM报文类型列表，如 "1005(1),1074(120),1084(120)"
    std::string _nav_system;      // 卫星系统，如 "GPS+GLO+GAL+BDS"

public:
    server_status(std::string uid)
    {
        _uid = uid;

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
        _alias_mpt = alias_mpt;
        return 0;
    }
    int set_coord_info(double ecef_x, double exef_y, double ecef_z)
    {
        _ecef_x = ecef_x;
        _ecef_y = exef_y;
        _ecef_z = ecef_z;
        _position_update_time = util_get_now_second();
        return 0;
    }

    int set_source_info(const std::string &format_details, const std::string &nav_system)
    {
        if (!format_details.empty())
            _format_details = format_details;
        if (!nav_system.empty())
            _nav_system = nav_system;
        _update_time = util_get_now_second();
        return 0;
    }

    int fromString(const std::string &str)
    {
        caster::core::ServerState proto;
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
        return 0;
    }
    std::string toString()
    {
        _update_time = util_get_now_second();

        caster::core::ServerState proto;
        proto.set_uid(_uid);
        proto.set_online_time(static_cast<uint64_t>(_online_time));
        proto.set_update_time(static_cast<uint64_t>(_update_time));
        proto.set_login_mpt(_login_mpt);
        proto.set_alias_mpt(_alias_mpt);
        proto.set_type(_type);
        proto.set_account(_account);
        proto.set_ip(_ip);
        proto.set_port(_port);
        proto.set_online_seconds(static_cast<uint64_t>(_update_time - _online_time));
        proto.set_ecef_x(_ecef_x);
        proto.set_ecef_y(_ecef_y);
        proto.set_ecef_z(_ecef_z);
        proto.set_position_update_time(static_cast<int64_t>(_position_update_time));
        return ProtoToJson(proto);
    }

    std::string toSource()
    {
        // 创建一个proto
        caster::core::SourceRecord proto;
        // 设置信息
        proto.set_uid(_uid);
        proto.set_mountpoint(_login_mpt);
        proto.set_update_time(static_cast<uint64_t>(_update_time));
        proto.set_decode_type(caster::SOURCE_DECODE_TYPE_AUTO);
        // RTCM解析出的报文和卫星系统信息
        if (!_format_details.empty())
            proto.set_format_details(_format_details);
        if (!_nav_system.empty())
            proto.set_nav_system(_nav_system);
        // 如果有坐标信息，转换为经纬度
        if (_position_update_time > 0)
        {
            double lat = 0.0, lon = 0.0, alt = 0.0;
            util_ecef2pos(_ecef_x, _ecef_y, _ecef_z, lat, lon, alt);
            proto.set_latitude(std::to_string(lat));
            proto.set_longitude(std::to_string(lon));
        }
        // 生成json
        return ProtoToJson(proto);
    }
};
