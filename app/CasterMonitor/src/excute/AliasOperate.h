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



class EventAddAlias : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY_AUTO(QVariantMap, alias_info)
public:
    explicit EventAddAlias(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {

        auto UID= m_alias_info["UID"].toString();
        auto info= JsonToQString(variantMapToJson(m_alias_info)) ;

        redisAsyncCommand(ctx, Redis_Add_Alias_Callback, this, "HSETNX MPT:ALIAS %s %s",UID.toStdString().c_str(),info.toStdString().c_str());

    }

public:
    static void Redis_Add_Alias_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventAddAlias *>(privdata);

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


class EventSetAlias : public RedisOperationBase
{
public:
    explicit EventSetAlias(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


class EventGetAlias : public RedisOperationBase
{
public:
    explicit EventGetAlias(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


class EventDelAlias : public RedisOperationBase
{
public:
    explicit EventDelAlias(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};
