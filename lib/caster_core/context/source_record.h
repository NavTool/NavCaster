#pragma once
#include "context_util.h"

// 源列表信息
class source_record
{
private:
    std::string _uid;

public:
    source_record(std::string uid)
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
        caster::core::SourceRecord proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
