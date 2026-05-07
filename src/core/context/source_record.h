#pragma once
#include "context_util.h"

// 源列表信息
class source_record
{
private:
    std::string _uid;
    std::string _source_group_uid = "default";

    std::string _mountpoint;      // 挂载点名称
    std::string _latitude;        // 纬度
    std::string _longitude;       // 经度
    std::string _format_details;  // RTCM报文类型，如 "1005(1),1074(120)"
    std::string _nav_system;      // 卫星系统，如 "GPS+GLO+GAL+BDS"
    double _ecef_x = 0.0;
    double _ecef_y = 0.0;
    double _ecef_z = 0.0;

public:
    source_record(std::string uid)
    {
        _uid = uid;
    }

    void set_mountpoint(const std::string &mpt) { _mountpoint = mpt; }
    const std::string &mountpoint() const { return _mountpoint; }
    const std::string &source_group_uid() const { return _source_group_uid; }

    bool get_ecef_coord(double &ecef_x, double &ecef_y, double &ecef_z) const
    {
        if (_ecef_x == 0.0 && _ecef_y == 0.0 && _ecef_z == 0.0)
        {
            return false;
        }
        ecef_x = _ecef_x;
        ecef_y = _ecef_y;
        ecef_z = _ecef_z;
        return true;
    }

    int fromString(const std::string &str)
    {
        caster::core::SourceRecord proto;
        if (!JsonToProto(str, proto))
        {
            return 1;
        }
        _uid = proto.uid();
        _source_group_uid = proto.source_group_uid().empty() ? "default" : proto.source_group_uid();
        _mountpoint = proto.mountpoint();
        if (!proto.latitude().empty())
            _latitude = proto.latitude();
        if (!proto.longitude().empty())
            _longitude = proto.longitude();
        if (!proto.format_details().empty())
            _format_details = proto.format_details();
        if (!proto.nav_system().empty())
            _nav_system = proto.nav_system();
        _ecef_x = proto.ecef_x();
        _ecef_y = proto.ecef_y();
        _ecef_z = proto.ecef_z();
        return 0;
    }
    std::string toString()
    {
        caster::core::SourceRecord proto;
        proto.set_uid(_uid);
        proto.set_source_group_uid(_source_group_uid);
        proto.set_mountpoint(_mountpoint);
        if (!_latitude.empty())
            proto.set_latitude(_latitude);
        if (!_longitude.empty())
            proto.set_longitude(_longitude);
        if (!_format_details.empty())
            proto.set_format_details(_format_details);
        if (!_nav_system.empty())
            proto.set_nav_system(_nav_system);
        proto.set_ecef_x(_ecef_x);
        proto.set_ecef_y(_ecef_y);
        proto.set_ecef_z(_ecef_z);
        return ProtoToJson(proto);
    }

    std::string toSourceItem() const
    {
        auto info = build_default_mount_info(_mountpoint);
        if (!_latitude.empty())
            info.latitude = _latitude;
        if (!_longitude.empty())
            info.longitude = _longitude;
        if (!_format_details.empty())
            info.format_details = _format_details;
        if (!_nav_system.empty())
            info.nav_system = _nav_system;
        return convert_mount_info_to_string(info);
    }
};
