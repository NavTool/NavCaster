#pragma once
#include "context_util.h"

// 别名规则
class alias_rule
{
private:
    std::string _uid;

public:
    alias_rule(std::string uid)
    {
        _uid=uid;
    }

    int fromString(const std::string &str)
    {
        return 0;
    }
    std::string toString()
    {
        // 创建一个proto
        caster::core::AliasRule proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
