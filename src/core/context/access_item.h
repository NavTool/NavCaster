#pragma once
#include "context_util.h"

// 账号分组条目
class access_item
{
private:
    std::string _uid;
    std::string _json; // 保存完整的 AccessItem JSON

public:
    access_item(std::string uid)
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
        caster::core::AccessItem proto;
        proto.set_uid(_uid);
        return ProtoToJson(proto);
    }
};
