#pragma once
#include "Caster_Core.h"
#include "context_util.h"

// 广播消息
class boardcast_msg
{
public:

    // 广播的类型
    CasterBroadcastType type = CasterBroadcastType::UNKNOWN; // 0:未知 1:基站 2:  3:  4:  5:
    // 目标ConnectKey
    std::string connect_key;
    // 目标频道
    std::string channel;
    // 传递参数
    std::string Para;

    // 状态
    CasterReply status = CasterReply::NIL;
    // 原因
    std::string reason;



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
