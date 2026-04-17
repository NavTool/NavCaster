#pragma once
#include "context_util.h"

// 账号分组
class access_group
{
private:
    std::string _uid;
    std::string _json; // 保存完整的 AccessGroup JSON

public:
    access_group(std::string uid)
        : _uid(std::move(uid))
    {
    }

    int fromString(const std::string &str)
    {
        _json = str;
        return 0;
    }

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
