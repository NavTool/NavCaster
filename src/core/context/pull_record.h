#pragma once
#include "context_util.h"

// 数据拉取配置
class pull_record
{
private:
    std::string _uid;

public:
    pull_record(std::string uid)
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
        caster::core::PullRecord proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
