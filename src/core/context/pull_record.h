#pragma once
#include "context_util.h"

// 数据拉取配置
class pull_record
{
private:
    std::string _uid;
    std::string _json; // 保存完整的 PullRecord JSON

public:
    pull_record(std::string uid)
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
        caster::core::PullRecord proto;
        proto.set_uid(_uid);
        return ProtoToJson(proto);
    }

    bool is_enabled() const
    {
        if (_json.empty())
            return false;
        caster::core::PullRecord proto;
        if (!JsonToProto(_json, proto))
            return false;
        return proto.enabled();
    }
};
