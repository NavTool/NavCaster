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

    // Q_INVOKABLE QString name() const override { return typeid(this).name(); }

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
        redisAsyncCommand(_redis_context, Redis_Auth_Cb, this, "AUTH %s", m_auth.toStdString().c_str());
    }

public:

    Q_SIGNAL void connectRedisSuccess();    // 连接成功
    Q_SIGNAL void updateRedisCtx(redisAsyncContext *ctx);
    Q_SIGNAL void connectRedisFailed();     // 连接失败
    Q_SIGNAL void authRedisReply(std::string);

public:
    // redis回调
    static void Redis_Connect_Cb(const redisAsyncContext *c, int status)
    {
        auto svr = static_cast<EventConnectRedis*>(c->data);

        if (status == REDIS_OK)
        {
            svr->_is_connected = true;

            svr->updateRedisCtx(svr->_redis_context);   // 发信号传值
            svr->connectRedisSuccess();                 // 发连接成功信号
        }
        else
        {
            svr->_is_connected = false;
            svr->_context_errstr = c->err;

            svr->connectRedisFailed();                  // 发送连接失败信号
        }
    };
    static void Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
    {
        auto svr = static_cast<EventConnectRedis*>(c->data);

    };

    static void Redis_Auth_Cb(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventConnectRedis *>(privdata);


        if (!reply)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_STRING)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_ARRAY)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_INTEGER)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_NIL)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_STATUS)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_ERROR)
        {
            svr->authRedisReply(reply->str);
            return;
        }
        if (reply->type == REDIS_REPLY_DOUBLE)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_BOOL)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_MAP)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_SET)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_ATTR)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_PUSH)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_BIGNUM)
        {
            return;
        }
        if (reply->type == REDIS_REPLY_VERB)
        {
            return;
        }
    }


public:
    bool _is_connected = false;
    int _reconnect_count = 0; // 重连计数  连接成功后归零   重连失败后，等待时间0、2、4、8、10（max）
    std::string _context_errstr;
    redisAsyncContext *_redis_context = nullptr;
};


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

    // Q_INVOKABLE QString name() const override { return typeid(this).name(); }

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


