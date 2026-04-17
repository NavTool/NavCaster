#pragma once
#include "context_util.h"

// 账号分组条目
class access_item
{
private:
    std::string _uid;

public:
    access_item(std::string uid)
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
        caster::core::AccessItem proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
