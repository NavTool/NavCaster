#pragma once
#include "Caster_Core.h"
#include "context_util.h"

// 广播消息
class broadcast_msg
{
public:
    caster::core::BroadcastType type = caster::core::BOARDCAST_TYPE_UNSPECIFIED;           // 广播类型
    caster::core::BroadcastOperateType operate = caster::core::BOARDCAST_OPERATE_UNSPECIFIED; // 操作类型
    std::string target;                     // 相关对象的唯一标识，例如节点ID、连接ID等
    std::string msg_str;                    // 请求字符串: 对于PULL/PUSH任务为JSON格式的Proto数据，对于状态变更为channel
    std::string reason_str;                 // 原因

    broadcast_msg() = default;

    int fromString(const std::string &str)
    {
        caster::core::BroadcastMsg proto;
        if (!JsonToProto(str, proto))
        {
            return 1; // 解析失败
        }
        type = proto.type();
        operate = proto.operate();
        target = proto.target();
        msg_str = proto.msg_str();
        reason_str = proto.reason_str();
        return 0;
    }

    std::string getMsgStr() const
    {
        return msg_str;
    }

    std::string toString() const
    {
        caster::core::BroadcastMsg proto;
        proto.set_type(type);
        proto.set_operate(operate);
        proto.set_target(target);
        proto.set_msg_str(msg_str);
        proto.set_reason_str(reason_str);
        return ProtoToJson(proto);
    }

    // 从CasterReply转换为BroadcastOperateType
    static caster::core::BroadcastOperateType ReplyToOperate(CasterReply status)
    {
        switch (status)
        {
        case CasterReply::ACTIVE:
            return caster::core::BOARDCAST_OPERATR_ACTIVE;
        case CasterReply::INACTIVE:
            return caster::core::BOARDCAST_OPERATR_INACTIVE;
        default:
            return caster::core::BOARDCAST_OPERATE_UNSPECIFIED;
        }
    }

    // 从BroadcastOperateType转换为CasterReply
    static CasterReply OperateToReply(caster::core::BroadcastOperateType op)
    {
        switch (op)
        {
        case caster::core::BOARDCAST_OPERATR_ACTIVE:
            return CasterReply::ACTIVE;
        case caster::core::BOARDCAST_OPERATR_INACTIVE:
            return CasterReply::INACTIVE;
        default:
            return CasterReply::OK;
        }
    }
};
