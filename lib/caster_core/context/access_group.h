#pragma once
#include "context_util.h"

// 账号分组
class access_group
{
private:
    std::string _uid;

public:
    access_group(std::string uid)
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
        caster::core::AccessGroup proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
