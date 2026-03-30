#pragma once

#include <event2/util.h>
#include <QObject>
#include <QtQml/qqml.h>
#include <QUuid>
#include "EventOperationBase.h"
#include "OperateMap.h"
#include <functional>
#include <list>
#include <mutex>
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

// ============================================================================
//  RedisHash —— 单次 Redis HASH 命令的异步操作封装
// ============================================================================
template <const char *Table>
class RedisHash : public RedisOperationBase
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
        id(QUuid::createUuid().toString(QUuid::WithoutBraces));
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

    void execute(redisAsyncContext *ctx)
    {
        switch (_type)
        {
        case HashOperateType::ADD:
            redisAsyncCommand(ctx, Redis_Add_Callback, this,
                              "HSETNX %s %s %s", _key.c_str(), _field.c_str(), _value.c_str());
            break;
        case HashOperateType::DEL:
            redisAsyncCommand(ctx, Redis_Del_Callback, this,
                              "HDEL %s %s", _key.c_str(), _field.c_str());
            break;
        case HashOperateType::SET:
            redisAsyncCommand(ctx, Redis_Set_Callback, this,
                              "HSET %s %s %s", _key.c_str(), _field.c_str(), _value.c_str());
            break;
        case HashOperateType::GET:
            redisAsyncCommand(ctx, Redis_Get_Callback, this,
                              "HGET %s %s", _key.c_str(), _field.c_str());
            break;
        case HashOperateType::GET_ALL:
            redisAsyncCommand(ctx, Redis_GetAll_Callback, this,
                              "HGETALL %s", _key.c_str());
            break;
        default:
            break;
        }
    }

    // HSETNX → INTEGER: 1=新增成功, 0=字段已存在
    static void Redis_Add_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);
        if (!reply)
            return;

        if (reply->type != REDIS_REPLY_INTEGER)
        {
            Q_EMIT svr->operateFinished(svr->id(), false, QVariantMap());
            return;
        }
        Q_EMIT svr->operateFinished(svr->id(), reply->integer == 1, QVariantMap());
    }

    // HDEL → INTEGER: 被删除的字段数
    static void Redis_Del_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);
        if (!reply)
            return;

        if (reply->type != REDIS_REPLY_INTEGER)
        {
            Q_EMIT svr->operateFinished(svr->id(), false, QVariantMap());
            return;
        }
        Q_EMIT svr->operateFinished(svr->id(), reply->integer >= 1, QVariantMap());
    }

    // HSET → INTEGER: 1=新字段, 0=已有字段被更新; 两者均为写入成功
    static void Redis_Set_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);
        if (!reply)
            return;

        if (reply->type != REDIS_REPLY_INTEGER)
        {
            Q_EMIT svr->operateFinished(svr->id(), false, QVariantMap());
            return;
        }
        Q_EMIT svr->operateFinished(svr->id(), true, QVariantMap());
    }

    // HGET → BULK STRING / NIL
    static void Redis_Get_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);
        if (!reply)
            return;

        if (reply->type == REDIS_REPLY_NIL)
        {
            Q_EMIT svr->operateFinished(svr->id(), false, QVariantMap());
            return;
        }
        if (reply->type == REDIS_REPLY_STRING && reply->str)
        {
            QVariantMap data;
            data[QString::fromStdString(svr->_field)] =
                QString::fromUtf8(reply->str, static_cast<int>(reply->len));
            Q_EMIT svr->operateFinished(svr->id(), true, data);
            return;
        }
        Q_EMIT svr->operateFinished(svr->id(), false, QVariantMap());
    }

    // HGETALL → ARRAY (field, value 交替排列)
    static void Redis_GetAll_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<RedisHash<Table> *>(privdata);
        if (!reply)
            return;

        if (reply->type == REDIS_REPLY_NIL)
        {
            Q_EMIT svr->operateFinished(svr->id(), true, QVariantMap());
            return;
        }
        if (reply->type != REDIS_REPLY_ARRAY)
        {
            Q_EMIT svr->operateFinished(svr->id(), false, QVariantMap());
            return;
        }

        QVariantMap data;
        for (size_t i = 0; i + 1 < reply->elements; i += 2)
        {
            if (!reply->element[i]->str || !reply->element[i + 1]->str)
                continue;
            QString field = QString::fromUtf8(reply->element[i]->str,
                                              static_cast<int>(reply->element[i]->len));
            QString value = QString::fromUtf8(reply->element[i + 1]->str,
                                              static_cast<int>(reply->element[i + 1]->len));
            data[field] = value;
        }
        Q_EMIT svr->operateFinished(svr->id(), true, data);
    }
};

// ============================================================================
//  HashConetxt —— Redis Hash 表的本地上下文管理（模板类，非 QObject）
//  T     : protobuf 消息类型
//  Table : 编译期 Redis Hash key 名
// ============================================================================
template <typename T, const char *Table>
class HashConetxt
{
public:
    using HashOperateFinishedHandler = std::function<void(HashOperateType, QString, bool, QVariantMap)>;

private:
    std::unordered_map<std::string, std::shared_ptr<T>> m_obj_map;
    std::recursive_mutex m_map_lock;
    HashOperateFinishedHandler m_hash_operate_finished_handler;

public:
    // ==================== 异步操作接口 ====================

    std::string addObject(const std::string &field, std::string json_str)
    {
        return submitOperation(HashOperateType::ADD, field, json_str);
    }

    std::string delObject(const std::string &field)
    {
        return submitOperation(HashOperateType::DEL, field);
    }

    std::string setObject(const std::string &field, std::string json_str)
    {
        return submitOperation(HashOperateType::SET, field, json_str);
    }

    std::string getObject(const std::string &field)
    {
        return submitOperation(HashOperateType::GET, field);
    }

    std::string getAllObjects()
    {
        return submitOperation(HashOperateType::GET_ALL, "");
    }

    // ==================== 本地缓存查询 ====================

    std::string getObjectInfo(const std::string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(key);
        if (it == m_obj_map.end())
            return std::string();
        return ProtoToJson(*it->second);
    }

    std::shared_ptr<T> getLocalObject(const std::string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        auto it = m_obj_map.find(key);
        return (it != m_obj_map.end()) ? it->second : nullptr;
    }

    bool contains(const std::string &key)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return m_obj_map.find(key) != m_obj_map.end();
    }

    size_t size()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        return m_obj_map.size();
    }

    void forEach(const std::function<void(const std::string &, const std::shared_ptr<T> &)> &callback)
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        for (const auto &[key, obj] : m_obj_map)
        {
            callback(key, obj);
        }
    }

    std::unordered_map<std::string, std::shared_ptr<T>> *getObjs()
    {
        return &m_obj_map;
    }

    void clear()
    {
        std::lock_guard<std::recursive_mutex> lock(m_map_lock);
        m_obj_map.clear();
    }

    // ==================== 回调注册 ====================

    void setNoticeHashOperateFinishedHandler(HashOperateFinishedHandler handler)
    {
        m_hash_operate_finished_handler = std::move(handler);
    }

private:
    // ==================== 操作提交（内部） ====================

    std::string submitOperation(HashOperateType type,
                                const std::string &field,
                                const std::string &json_str = "")
    {
        auto hash_operate = std::make_shared<RedisHash<Table>>();

        switch (type)
        {
        case HashOperateType::ADD:
            hash_operate->init("add");
            hash_operate->add(field, json_str);
            break;
        case HashOperateType::DEL:
            hash_operate->init("del");
            hash_operate->del(field);
            break;
        case HashOperateType::SET:
            hash_operate->init("set");
            hash_operate->set(field, json_str);
            break;
        case HashOperateType::GET:
            hash_operate->init("get");
            hash_operate->get(field);
            break;
        case HashOperateType::GET_ALL:
            hash_operate->init("get_all");
            hash_operate->get_all();
            break;
        default:
            return {};
        }

        QObject::connect(hash_operate.get(), &RedisHash<Table>::operateFinished,
                         [this, type, field, json_str](QString OP_UID, bool success, QVariantMap info) {
                             onOperateFinished(type, field, json_str, OP_UID, success, info);
                         });

        return Operaters::getInstance()->addRedisOperate(hash_operate);
    }

    // ==================== 统一回调处理 ====================

    void onOperateFinished(HashOperateType type,
                           const std::string &field,
                           const std::string &json_str,
                           QString OP_UID, bool success, QVariantMap info)
    {
        Operaters::getInstance()->deleteRedisOperate(OP_UID.toStdString());

        if (success)
        {
            std::lock_guard<std::recursive_mutex> lock(m_map_lock);
            switch (type)
            {
            case HashOperateType::ADD:
            case HashOperateType::SET:
            {
                auto obj = std::make_shared<T>();
                if (JsonToProto(json_str, *obj))
                {
                    m_obj_map[field] = obj;
                }
                break;
            }
            case HashOperateType::DEL:
            {
                m_obj_map.erase(field);
                break;
            }
            case HashOperateType::GET:
            {
                for (auto it = info.begin(); it != info.end(); ++it)
                {
                    auto obj = std::make_shared<T>();
                    if (JsonToProto(it.value().toString().toStdString(), *obj))
                    {
                        m_obj_map[it.key().toStdString()] = obj;
                    }
                }
                break;
            }
            case HashOperateType::GET_ALL:
            {
                m_obj_map.clear();
                for (auto it = info.begin(); it != info.end(); ++it)
                {
                    auto obj = std::make_shared<T>();
                    if (JsonToProto(it.value().toString().toStdString(), *obj))
                    {
                        m_obj_map[it.key().toStdString()] = obj;
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        noticeHashOperateFinished(type, OP_UID, success, info);
    }

    void noticeHashOperateFinished(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
    {
        if (m_hash_operate_finished_handler)
        {
            m_hash_operate_finished_handler(type, OP_UID, success, info);
        }
    }
};
