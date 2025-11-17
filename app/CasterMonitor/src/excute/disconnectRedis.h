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

class EventDisconnectRedis : public EventOperationBase
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit EventDisconnectRedis(QObject *parent = nullptr): EventOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(event_base* base) override {

        if(_redis_context==nullptr)
        {
            disconnectRedisFailed();
        }

        // 更新回调函数
        redisAsyncSetConnectCallback(_redis_context, Redis_Connect_Cb);
        redisAsyncSetDisconnectCallback(_redis_context, Redis_Disconnect_Cb);

        // 执行释放函数
        redisAsyncDisconnect(_redis_context);
    }

    int set_redis_ctx(redisAsyncContext *redis_context)
    {
        _redis_context=redis_context;

        return 0;
    }

public:
    Q_SIGNAL void disconnectRedisFailed();      // 断开连接失败
    Q_SIGNAL void disconnectRedisSuccess();     // 连接成功

public:
    // redis回调
    static void Redis_Connect_Cb(const redisAsyncContext *c, int status)
    {
        auto ctx = static_cast<EventDisconnectRedis*>(c->data);
    };
    static void Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
    {
        auto ctx = static_cast<EventDisconnectRedis*>(c->data);
        ctx->disconnectRedisSuccess();
    };

public:
    std::string _context_errstr;
    redisAsyncContext *_redis_context = nullptr;
};


