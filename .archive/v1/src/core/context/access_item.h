#pragma once
#include "context_util.h"
#include <nlohmann/json.hpp>

// 账号分组条目
class access_item
{
private:
    std::string _uid;
    std::string _mount_point_name;
    caster::core::AccessState _allow_visible = caster::core::ACCESS_STATE_DEFALT;
    caster::core::AccessState _allow_access = caster::core::ACCESS_STATE_DEFALT;
    caster::core::AccessState _allow_nearby = caster::core::ACCESS_STATE_DEFALT;
    std::string _json; // 保存完整的 AccessItem JSON

public:
    access_item(std::string uid)
        : _uid(std::move(uid))
    {
    }

    int fromString(const std::string &str)
    {
        _json = str;
        try
        {
            auto info = nlohmann::json::parse(str);
            _uid = info.value("uid", _uid);
            _mount_point_name = info.value("mount_point_name", info.value("mountpoint", info.value("mount", _mount_point_name)));
            _allow_visible = static_cast<caster::core::AccessState>(info.value("allow_visible", static_cast<int>(_allow_visible)));
            _allow_access = static_cast<caster::core::AccessState>(info.value("allow_access", static_cast<int>(_allow_access)));
            _allow_nearby = static_cast<caster::core::AccessState>(info.value("allow_nearby", static_cast<int>(_allow_nearby)));
            if (_uid.empty())
            {
                _uid = _mount_point_name;
            }
            return _mount_point_name.empty() ? 1 : 0;
        }
        catch (...)
        {
            return 1;
        }
    }

    const std::string &uid() const { return _uid; }
    const std::string &mount_point_name() const { return _mount_point_name; }
    caster::core::AccessState allow_visible() const { return _allow_visible; }
    caster::core::AccessState allow_access() const { return _allow_access; }
    caster::core::AccessState allow_nearby() const { return _allow_nearby; }

    std::string toString() const
    {
        if (!_json.empty())
            return _json;
        // fallback: 仅有 uid 时构造最小 JSON
        caster::core::AccessItem proto;
        proto.set_uid(_uid);
        return ProtoToJson(proto);
    }
};
