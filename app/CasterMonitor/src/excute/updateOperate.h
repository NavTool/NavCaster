


#include "EventOperationBase.h"
#include <qqmlintegration.h>


class EventUpdateNodeData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateNodeData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL CASTER:NODE ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateNodeData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};


class EventUpdateAccountData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateAccountData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL ACT:ACCOUNT ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateAccountData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};


class EventUpdateClientData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateClientData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL USR:STAT ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateClientData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};


class EventUpdateServerData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateServerData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL MPT:STAT ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateServerData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};


class EventUpdateRelayPullData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateRelayPullData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL ACT:ACCOUNT ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateRelayPullData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};


class EventUpdateRelayPushData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateRelayPushData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL ACT:ACCOUNT ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateRelayPushData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};


class EventUpdateAliasRuleData : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT
public:
    explicit EventUpdateAliasRuleData(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override
    {
        redisAsyncCommand(ctx, Redis_Update_Data_Callback, this, "HGETALL ACT:ACCOUNT ");
    }

    static void Redis_Update_Data_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventUpdateAliasRuleData *>(privdata);

        auto&data = svr->m_data;

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

            data[field]=value;
        }

        // 更新数据
        Q_EMIT svr->operateFinished(svr->id(),true,data);
    }

public:
    QVariantMap  m_data;

};
