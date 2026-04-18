#pragma once
#include "context_util.h"

// 数据拉取状态
class pull_status
{
private:
    std::string _uid;
    std::string _connect_key;
    int _state = 0;
    std::time_t _create_time = 0;
    std::time_t _update_time = 0;
    std::string _node_uid;
    std::string _node_name;

public:
    pull_status(std::string uid)
        : _uid(std::move(uid))
    {
        _create_time = util_get_now_second();
    }

    const std::string &uid() const
    {
        return _uid;
    }

    int set_node_info(const std::string &node_uid, const std::string &node_name)
    {
        _node_uid = node_uid;
        _node_name = node_name;
        return 0;
    }

    int update_state(std::string connect_key, int state)
    {
        _connect_key = connect_key;
        _state = state;
        _update_time = util_get_now_second();
        return 0;
    }

    int fromString(const std::string &str)
    {
        caster::core::PullState proto;
        if (!JsonToProto(str, proto))
            return 1;
        _uid = proto.uid();
        _create_time = proto.create_time();
        _update_time = proto.update_time();
        _connect_key = proto.connect_key();
        _state = proto.state();
        _node_uid = proto.node_uid();
        _node_name = proto.node_name();
        return 0;
    }

    std::string toString()
    {
        _update_time = util_get_now_second();
        caster::core::PullState proto;
        proto.set_uid(_uid);
        proto.set_create_time(static_cast<uint64_t>(_create_time));
        proto.set_update_time(static_cast<uint64_t>(_update_time));
        proto.set_connect_key(_connect_key);
        proto.set_state(_state);
        proto.set_node_uid(_node_uid);
        proto.set_node_name(_node_name);
        return ProtoToJson(proto);
    }
};
