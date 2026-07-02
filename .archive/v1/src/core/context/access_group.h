#pragma once
#include "context_util.h"
#include <nlohmann/json.hpp>

// 账号分组
class access_group
{
private:
    std::string _uid;
    std::string _group_name;
    std::string _json; // 保存完整的 AccessGroup JSON
    bool _nearest_mpt_enable = false;
    std::string _nearest_mpt_source_name;
    bool _allow_visible_inside_group = true;
    bool _allow_access_inside_group = true;
    bool _allow_nearby_inside_group = true;
    bool _allow_visible_outside_group = true;
    bool _allow_access_outside_group = true;
    bool _allow_nearby_outside_group = true;

public:
    access_group(std::string uid)
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
            _group_name = info.value("group_name", _uid);
            _nearest_mpt_enable = info.value("nearest_mpt_enable", _nearest_mpt_enable);
            _nearest_mpt_source_name = info.value("nearest_mpt_source_name", _nearest_mpt_source_name);
            _allow_visible_inside_group = info.value("allow_visible_inside_group", _allow_visible_inside_group);
            _allow_access_inside_group = info.value("allow_access_inside_group", _allow_access_inside_group);
            _allow_nearby_inside_group = info.value("allow_nearby_inside_group", _allow_nearby_inside_group);
            _allow_visible_outside_group = info.value("allow_visible_outside_group", _allow_visible_outside_group);
            _allow_access_outside_group = info.value("allow_access_outside_group", _allow_access_outside_group);
            _allow_nearby_outside_group = info.value("allow_nearby_outside_group", _allow_nearby_outside_group);
            if (_uid.empty())
            {
                _uid = _group_name;
            }
            return 0;
        }
        catch (...)
        {
            return 1;
        }
    }

    const std::string &uid() const { return _uid; }
    const std::string &group_name() const { return _group_name; }
    bool nearest_mpt_enable() const { return _nearest_mpt_enable; }
    const std::string &nearest_mpt_source_name() const { return _nearest_mpt_source_name; }
    bool allow_visible_inside_group() const { return _allow_visible_inside_group; }
    bool allow_access_inside_group() const { return _allow_access_inside_group; }
    bool allow_nearby_inside_group() const { return _allow_nearby_inside_group; }
    bool allow_visible_outside_group() const { return _allow_visible_outside_group; }
    bool allow_access_outside_group() const { return _allow_access_outside_group; }
    bool allow_nearby_outside_group() const { return _allow_nearby_outside_group; }

    std::string toString() const
    {
        if (!_json.empty())
            return _json;
        // fallback: 仅有 uid 时构造最小 JSON
        caster::core::AccessGroup proto;
        proto.set_uid(_uid);
        return ProtoToJson(proto);
    }
};
