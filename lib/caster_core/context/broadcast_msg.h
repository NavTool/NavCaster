#pragma once
#include "Caster_Core.h"
#include "context_util.h"


// 广播消息
class broadcast_msg
{
public:

BroadcastMsg _msg;



public:


    int fromString(const std::string &str)
    {
        return 0;
    }
    std::string toString()
    {
        // 创建一个proto
        caster::core::BoardcastMsg proto;
        // 设置信息
        // 生成json
        return ProtoToJson(proto);
    }
};
