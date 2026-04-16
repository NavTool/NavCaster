#pragma once
#include "context_util.h"

// 源列表信息
class source_record
{
private:
    std::string _uid;

    std::string _mountpoint;      // 挂载点名称
    std::string _latitude;        // 纬度
    std::string _longitude;       // 经度
    std::string _format_details;  // RTCM报文类型，如 "1005(1),1074(120)"
    std::string _nav_system;      // 卫星系统，如 "GPS+GLO+GAL+BDS"

public:
    source_record(std::string uid)
    {
        _uid = uid;
    }

    int fromString(const std::string &str)
    {
        caster::core::SourceRecord proto;
        if (!JsonToProto(str, proto))
        {
            return 1;
        }
        _uid = proto.uid();
        _mountpoint = proto.mountpoint();
        if (!proto.latitude().empty())
            _latitude = proto.latitude();
        if (!proto.longitude().empty())
            _longitude = proto.longitude();
        if (!proto.format_details().empty())
            _format_details = proto.format_details();
        if (!proto.nav_system().empty())
            _nav_system = proto.nav_system();
        return 0;
    }
    std::string toString()
    {
        caster::core::SourceRecord proto;
        proto.set_uid(_uid);
        proto.set_mountpoint(_mountpoint);
        if (!_latitude.empty())
            proto.set_latitude(_latitude);
        if (!_longitude.empty())
            proto.set_longitude(_longitude);
        if (!_format_details.empty())
            proto.set_format_details(_format_details);
        if (!_nav_system.empty())
            proto.set_nav_system(_nav_system);
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
