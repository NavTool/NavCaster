#pragma once

#include <event2/util.h>
#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "EventOperationBase.h"
#include "OperateMap.h"
#include <functional>
#include <list>
#include <string>

enum class HashOperateType
{
    UNKNOWN = 0,
    ADD = 1,
    DEL = 2,
    SET = 3,
    GET = 4,
    GET_ALL = 5
};

template <const char *Table>
class RedisHash: public RedisOperationBase
{
private:
    std::string _key = Table;
    std::string _field;
    std::string _value;

    QVariantMap m_data;

    HashOperateType _type = HashOperateType::UNKNOWN;

public:
    int init(std::string operate_name)
    {
        name(operate_name.c_str());
        return 0;
    }

    int add(std::string field, std::string value)
    {
        _type = HashOperateType::ADD;
        _field = field;
        _value = value;
        return 0;
    }

    int del(std::string field)
    {
        _type = HashOperateType::DEL;
        _field = field;
        return 0;
    }

    int set(std::string field, std::string value)
    {
        _type = HashOperateType::SET;
        _field = field;
        _value = value;
        return 0;
    }

    int get(std::string field)
    {
        _type = HashOperateType::GET;
        _field = field;
        return 0;
    }

    int get_all()
    {
        _type = HashOperateType::GET_ALL;
        return 0;
    }

    int get(std::string key, std::list<std::string> fileds)
    {
        return 0;
    }

    void execute(redisAsyncContext *ctx)
    {

        switch (_type)
        {
        case HashOperateType::ADD:
            redisAsyncCommand(ctx, Redis_Add_Callback, this, "HSETNX %s %s %s", _key.c_str(), _field.c_str(), _value.c_str());
            break;
        case HashOperateType::DEL:
            redisAsyncCommand(ctx, Redis_Del_Callback, this, "HDEL %s %s", _key.c_str(), _field.c_str());
            break;
        case HashOperateType::SET:
            redisAsyncCommand(ctx, Redis_Set_Callback, this, "HSETEX %s %s %s", _key.c_str(), _field.c_str(), _value.c_str());
            break;
        case HashOperateType::GET:
            redisAsyncCommand(ctx, Redis_Get_Callback, this, "HGET %s %s", _key.c_str(), _field.c_str());
            break;
        case HashOperateType::GET_ALL:
            redisAsyncCommand(ctx, Redis_Get_Callback, this, "HGETALL %s", _key.c_str());
            break;
        default:
            break;
        }
    }

    static void Redis_Add_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }

        if (reply->integer == 1)
        {
            // 添加成功
            Q_EMIT svr->operateFinished(svr->uid(), true, QVariantMap());
        }
        else
        {
            // 添加失败
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }
    }

    static void Redis_Del_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }

        if (reply->integer == 1)
        {
            // 添加成功
            Q_EMIT svr->operateFinished(svr->uid(), true, QVariantMap());
        }
        else
        {
            // 添加失败
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }
    }

    static void Redis_Set_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }

        if (reply->integer == 1)
        {
            // 添加成功
            Q_EMIT svr->operateFinished(svr->uid(), true, QVariantMap());
        }
        else
        {
            // 添加失败
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }
    }

    static void Redis_Get_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }

        if (reply->integer == 1)
        {
            // 添加成功
            Q_EMIT svr->operateFinished(svr->uid(), true, QVariantMap());
        }
        else
        {
            // 添加失败
            Q_EMIT svr->operateFinished(svr->uid(), false, QVariantMap());
        }
    }

    static void Redis_GetAll_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);

        auto &data = svr->m_data;

        data.clear();

        if (!reply)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_NIL)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_ARRAY)
        {
            return;
        }

        // 更新data
        for (int i = 0; i < reply->elements; i += 2)
        {
            QString field = reply->element[i]->str;
            QString value = reply->element[i + 1]->str;

            data[field] = value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->uid(), true, data);
    }
};


template <typename T, const char *Table>
class HashConetxt
{
    std::unordered_map<std::string, std::shared_ptr<T>> m_obj_map;
    std::recursive_mutex m_map_lock;

public:
    std::string addObject(const std::string &field, std::string json_str)
    {
        // 生成一个唯一的UID


        // 创建一个HashOperate对象
        auto hash_operate = std::make_shared<RedisHash<Table>>();
        hash_operate->init("add");
        hash_operate->add(field, json_str);

        // 连接信号和槽
        connect(hash_operate.get(), &RedisHash<Table>::operateFinished, this, &HashConetxt::onAddObjectFinished);

        // 添加到MAP中，等待任务执行
        return Operaters::getInstance()->addRedisOperate(hash_operate);
    }

    std::string delObject(const std::string &field)
    {
        // 创建一个HashOperate对象
        auto hash_operate = std::make_shared<RedisHash<Table>>();
        hash_operate->init("del");
        hash_operate->del(field);

        // 连接信号和槽
        connect(hash_operate.get(), &RedisHash<Table>::operateFinished, this, &HashConetxt::onDelObjectFinished);

        // 添加到MAP中，等待任务执行
        return Operaters::getInstance()->addRedisOperate(hash_operate);
    }

    std::string setObject(const std::string &field, std::string json_str)
    {
        // 创建一个HashOperate对象
        auto hash_operate = std::make_shared<RedisHash<Table>>();
        hash_operate->init("set");
        hash_operate->set(field, json_str);

        // 连接信号和槽
        connect(hash_operate.get(), &RedisHash<Table>::operateFinished, this, &HashConetxt::onSetObjectFinished);

        // 添加到MAP中，等待任务执行
        return Operaters::getInstance()->addRedisOperate(hash_operate);
    }

    std::string getObject(const std::string &field)
    {
        // 创建一个HashOperate对象
        auto hash_operate = std::make_shared<RedisHash<Table>>();
        hash_operate->init("get");
        hash_operate->get(field);

        // 连接信号和槽
        connect(hash_operate.get(), &RedisHash<Table>::operateFinished, this, &HashConetxt::onGetObjectFinished);

        // 添加到MAP中，等待任务执行
        return Operaters::getInstance()->addRedisOperate(hash_operate);
    }

    std::string getAllObjects()
    {

        // 创建一个HashOperate对象
        auto hash_operate = std::make_shared<RedisHash<Table>>();
        hash_operate->init("get_all");
        hash_operate->get_all();

        // 连接信号和槽
        connect(hash_operate.get(), &RedisHash<Table>::operateFinished, this, &HashConetxt::onGetObjectFinished);

        // 添加到MAP中，等待任务执行
        return Operaters::getInstance()->addRedisOperate(hash_operate);

    }

    std::string getObjectInfo(const std::string &UID)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto item = m_obj_map.find(UID);
        if (item == m_obj_map.end())
        {
            return std::string(); // 找不到
        }
        return ProtoToJson(*item->second);
    }

    void forEach(const std::function<void(const std::string &, const std::shared_ptr<T> &)> &callback)
    {
        // std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        // for (const auto &[key, st] : m_obj_map)
        // {
        //     callback(key, st);
        // }
    }

    std::unordered_map<std::string, std::shared_ptr<T>> *getObjs()
    {
        // std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        // return &m_obj_map;
    }

    void setUnpdateFlagFalse()
    {
        // std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        // for (auto iter : m_obj_map)
        // {
        //     iter.second->set_update_flag(false);
        // }
    }

    void clearUnpdateFlagFalse()
    {
        // std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        // auto it = m_obj_map.begin();
        // while (it != m_obj_map.end())
        // {
        //     if (it->second->update_flag() == false)
        //     {
        //         it = m_obj_map.erase(it); // 删除元素，并更新迭代器
        //     }
        //     else
        //     {
        //         ++it; // 仅在未删除时前进迭代器
        //     }
        // }
    }

private slots:

    void onAddObjectFinished(QString OP_UID, bool success, QVariantMap info)
    {
        // 根据OP_UID找到对应的操作对象
        auto op =  Operaters::getInstance()->getRedisOperate(OP_UID.toStdString());
        if (op)        {
            return; // 没有找到对应的操作对象
        }

        if (success)
        {
            // 添加成功，更新context上下文
            // 解析info，获取添加的对象信息
            // 创建一个对象，并添加到context上下文中
            // 这里需要根据具体的业务逻辑来解析info并创建对象
        }
        else
        {
            // 添加失败，记录日志或者进行其他处理
        }

        // 无论成功还是失败，都可以选择是否从map中删除这个操作对象
        Operaters::getInstance()->deleteRedisOperate(OP_UID.toStdString());
    }

    void onDelObjectFinished(QString OP_UID, bool success, QVariantMap info)
    {
        auto op =  Operaters::getInstance()->getRedisOperate(OP_UID.toStdString());
        if (op == nullptr)        {
            return; // 没有找到对应的操作对象
        }

        if (success)
        {
            // 删除成功，更新context上下文
            // 解析info，获取删除的对象信息
            // 从context上下文中删除对应的对象
            // 这里需要根据具体的业务逻辑来解析info并删除对象
        }
        else
        {
            // 删除失败，记录日志或者进行其他处理
        }

        // 无论成功还是失败，都可以选择是否从map中删除这个操作对象
        Operaters::getInstance()->deleteRedisOperate(OP_UID.toStdString());
    }

    void onSetObjectFinished(QString OP_UID, bool success, QVariantMap info)
    {
        auto op =  Operaters::getInstance()->getRedisOperate(OP_UID.toStdString());
        if (op == nullptr)        {
            return; // 没有找到对应的操作对象
        }
        if (success)
        {
            // 设置成功，更新context上下文
            // 解析info，获取设置的对象信息
            // 更新context上下文中的对应对象
            // 这里需要根据具体的业务逻辑来解析info并更新对象
        }
        else
        {
            // 设置失败，记录日志或者进行其他处理
        }

        // 无论成功还是失败，都可以选择是否从map中删除这个操作对象
        Operaters::getInstance()->deleteRedisOperate(OP_UID.toStdString());
    }

    void onGetObjectFinished(QString OP_UID, bool success, QVariantMap info)
    {
        auto op =  Operaters::getInstance()->getRedisOperate(OP_UID.toStdString());
        if (op == nullptr)        {
            return; // 没有找到对应的操作对象
        }
        if (success)
        {
            // 获取成功，处理获取到的信息
            // 解析info，获取对象信息
            // 这里需要根据具体的业务逻辑来解析info并处理对象信息
        }
        else
        {
            // 获取失败，记录日志或者进行其他处理
        }

        // 无论成功还是失败，都可以选择是否从map中删除这个操作对象
        Operaters::getInstance()->deleteRedisOperate(OP_UID.toStdString());

        // 完成后通知主线程更新UI或者进行其他操作
    }

    void onGetAllObjectsFinished(QString OP_UID, bool success, QVariantMap info)
    {
       
        auto op =  Operaters::getInstance()->getRedisOperate(OP_UID.toStdString());
        if (op == nullptr)        {
            return; // 没有找到对应的操作对象
        }
        if (success)
        {
            // 获取成功，处理获取到的信息
            // 解析info，获取所有对象信息
            // 这里需要根据具体的业务逻辑来解析info并处理对象信息
        }
        else
        {
            // 获取失败，记录日志或者进行其他处理
        }

        // 无论成功还是失败，都可以选择是否从map中删除这个操作对象
        Operaters::getInstance()->deleteRedisOperate(OP_UID.toStdString());
        // 完成后通知主线程更新UI或者进行其他操作
    }
};
