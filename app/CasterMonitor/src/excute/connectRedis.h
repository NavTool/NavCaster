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

class EventConnectRedis : public EventOperationBase
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QString, ip)            // IP
    Q_PROPERTY_AUTO(int, port)              // 端口
    Q_PROPERTY_AUTO(QString, auth)          // auth
    QML_ELEMENT

public:
    explicit EventConnectRedis(QObject *parent = nullptr): EventOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(event_base* base) override {
        // Q_UNUSED(base);

        // 执行任务逻辑
        // 初始化redis连接
        redisOptions options = {0};
        options.privdata=this;
        REDIS_OPTIONS_SET_TCP(&options, m_ip.toStdString().c_str(), m_port);
        struct timeval tv = {0};
        tv.tv_sec = 10;
        options.connect_timeout = &tv;

        _redis_context = redisAsyncConnectWithOptions(&options);
        if (_redis_context->err)
        {
            QMetaObject::invokeMethod(this, [this]() {
                emit connectRedisFailed();   // 显式用对象发射信号
            }, Qt::QueuedConnection);
            redisAsyncFree(_redis_context);
            _redis_context = nullptr;
        }
        _redis_context->data = this;

        redisLibeventAttach(_redis_context, base);
        redisAsyncSetConnectCallback(_redis_context, Redis_Connect_Cb);
        redisAsyncSetDisconnectCallback(_redis_context, Redis_Disconnect_Cb);
        redisAsyncCommand(_redis_context, NULL, NULL, "AUTH %s", m_auth.toStdString().c_str());
    }

public:

    Q_SIGNAL void connectRedisSuccess();    // 连接成功
    Q_SIGNAL void updateRedisCtx(redisAsyncContext *ctx);
    Q_SIGNAL void connectRedisFailed();     // 连接失败

public:
    // redis回调
    static void Redis_Connect_Cb(const redisAsyncContext *c, int status)
    {
        auto ctx = static_cast<EventConnectRedis*>(c->data);

        if (!ctx) return;

        ctx->updateRedisCtx(ctx->_redis_context);   // 发信号传值
        ctx->connectRedisSuccess();                 // 发连接成功信号
    };
    static void Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
    {
        auto ctx = static_cast<EventConnectRedis*>(c->data);

        ctx->connectRedisFailed();   // 显式用对象发射信号
    };

public:
    bool _isconnected = false;
    int _reconnect_count = 0; // 重连计数  连接成功后归零   重连失败后，等待时间0、2、4、8、10（max）
    std::string _context_errstr;
    redisAsyncContext *_redis_context = nullptr;
};


