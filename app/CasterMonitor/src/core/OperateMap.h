#pragma once

#include <unordered_map>
#include "EventOperationBase.h"

class Operaters
{
public:
    std::unordered_map<std::string, std::shared_ptr<EventOperationBase>> _event_map;
    std::unordered_map<std::string, std::shared_ptr<RedisOperationBase>> _redis_map;

    static Operaters *getInstance()
    {
        static Operaters instance;
        return &instance;
    }

    std::string addEventOperate(std::shared_ptr<EventOperationBase> op)
    {
        // 添加到MAP中，等待任务执行
        _event_map.insert(std::pair(op->id().toStdString(), op));
        return op->id().toStdString();
    }

    std::string addRedisOperate(std::shared_ptr<RedisOperationBase> op)
    {
        // 添加到MAP中，等待任务执行
        _redis_map.insert(std::pair(op->id().toStdString(), op));
        return op->id().toStdString();
    }

    int deleteEventOperate(std::string op_uid)
    {
        _event_map.erase(op_uid);
        return 0;
    }

    int deleteRedisOperate(std::string op_uid)
    {
        _redis_map.erase(op_uid);
        return 0;
    }

    std::shared_ptr<EventOperationBase> getEventOperate(std::string op_uid)
    {
        auto it = _event_map.find(op_uid);
        if (it != _event_map.end())
        {
            return it->second;
        }
        return nullptr;
    }

    std::shared_ptr<RedisOperationBase> getRedisOperate(std::string op_uid)
    {
        auto it = _redis_map.find(op_uid);
        if (it != _redis_map.end())
        {
            return it->second;
        }
        return nullptr;
    }

};
