#pragma once
#include "context_util.h"

// 数据拉取状态
class pull_status
{
private:
    std::string _uid;

    std::string _connect_key;
    int _state=0;
public:
    pull_status(std::string uid)
    {
        _uid=uid;
    }

    int update_state(std::string connect_key, int state)
    {
        _connect_key=connect_key;
        _state=state;
        return 0;
    }


    int fromString(const std::string &str)
    {
        return 0;
    }
    std::string toString()
    {
        // 创建一个proto
        caster::core::PullState proto;
        // 设置信息
        proto.set_uid(_uid);
        // 生成json
        return ProtoToJson(proto);
    }
};
