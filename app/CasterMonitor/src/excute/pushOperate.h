#pragma once
#include <event2/util.h>
#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "EventWorker.h"
#include "EventOperationBase.h"
#include "stdafx.h"
#include "CasterMonitor.h"


/*
 *      创建一个对象
 *      设置属性
 *      绑定回调
 *      执行操作
 */



class EventAddPush : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY_AUTO(QVariantMap, relay_info)
public:
    explicit EventAddPush(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

        void execute(redisAsyncContext *ctx) override {

        auto UID= m_relay_info["UID"].toString();
        auto info= JsonToQString(variantMapToJson(m_relay_info)) ;

        redisAsyncCommand(ctx, Redis_Add_Pull_Callback, this, "HSETNX STR:PUSH:LIST %s %s",UID.toStdString().c_str(),info.toStdString().c_str());

    }

public:
    static void Redis_Add_Pull_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventAddPush *>(privdata);

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->id(),false,QVariantMap());
        }

        if(reply->integer==1)
        {
            //添加成功
            Q_EMIT svr->operateFinished(svr->id(),true,QVariantMap());
        }
        else
        {
            //添加失败
            Q_EMIT svr->operateFinished(svr->id(),false,QVariantMap());
        }


    }

};


class EventSetPush : public RedisOperationBase
{
public:
    explicit EventSetPush(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


class EventGetPush : public RedisOperationBase
{
public:
    explicit EventGetPush(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


class EventDelPush : public RedisOperationBase
{
public:
    explicit EventDelPush(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};
