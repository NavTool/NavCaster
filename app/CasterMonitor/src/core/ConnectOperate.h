#pragma once
#include <event2/util.h>
#include <QObject>
#include <QRandomGenerator>
#include "EventWorker.h"
#include "EventOperationBase.h"
#include "stdafx.h"


// ============================================================================
//  EventConnectRedis — Redis 异步连接操作（基于 libevent）
// ============================================================================
class EventConnectRedis : public EventOperationBase
{
    Q_OBJECT
    Q_PROPERTY_AUTO(QString, ip)
    Q_PROPERTY_AUTO(int, port)
    Q_PROPERTY_AUTO(QString, auth)

public:
    explicit EventConnectRedis(QObject *parent = nullptr): EventOperationBase(parent) {};

    void execute(event_base* base) override {
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
                emit connectRedisFailed();
            }, Qt::QueuedConnection);
            redisAsyncFree(_redis_context);
            _redis_context = nullptr;
            return;
        }
        _redis_context->data = this;

        redisLibeventAttach(_redis_context, base);
        redisAsyncSetConnectCallback(_redis_context, Redis_Connect_Cb);
        redisAsyncSetDisconnectCallback(_redis_context, Redis_Disconnect_Cb);
        redisAsyncCommand(_redis_context, Redis_Auth_Cb, this, "AUTH %s", m_auth.toStdString().c_str());
    }

public:
    Q_SIGNAL void connectRedisSuccess();
    Q_SIGNAL void updateRedisCtx(redisAsyncContext *ctx);
    Q_SIGNAL void connectRedisFailed();
    Q_SIGNAL void authRedisReply(std::string);

public:
    static void Redis_Connect_Cb(const redisAsyncContext *c, int status)
    {
        auto svr = static_cast<EventConnectRedis*>(c->data);

        if (status == REDIS_OK)
        {
            svr->_is_connected = true;
            svr->updateRedisCtx(svr->_redis_context);
            svr->connectRedisSuccess();
        }
        else
        {
            svr->_is_connected = false;
            svr->_context_errstr = c->err;
            svr->connectRedisFailed();
        }
    };

    static void Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
    {
        Q_UNUSED(c);
        Q_UNUSED(status);
    };

    static void Redis_Auth_Cb(redisAsyncContext *c, void *r, void *privdata)
    {
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventConnectRedis *>(privdata);

        if (!reply)
        {
            svr->authRedisReply("AUTH reply is null");
            return;
        }
        if (reply->type == REDIS_REPLY_STATUS)
        {
            // AUTH succeeded (reply->str == "OK")
            return;
        }
        if (reply->type == REDIS_REPLY_ERROR)
        {
            svr->authRedisReply(reply->str);
            if (svr->_redis_context)
            {
                redisAsyncDisconnect(svr->_redis_context);
            }
            return;
        }
        svr->authRedisReply("unexpected AUTH reply type");
    }

public:
    bool _is_connected = false;
    int _reconnect_count = 0;
    std::string _context_errstr;
    redisAsyncContext *_redis_context = nullptr;
};


// ============================================================================
//  EventDisconnectRedis — Redis 异步断开操作
// ============================================================================
class EventDisconnectRedis : public EventOperationBase
{
    Q_OBJECT

public:
    explicit EventDisconnectRedis(QObject *parent = nullptr): EventOperationBase(parent) {};

    void execute(event_base* base) override {
        Q_UNUSED(base);

        if(_redis_context == nullptr)
        {
            disconnectRedisFailed();
            return;
        }

        redisAsyncSetConnectCallback(_redis_context, Redis_Connect_Cb);
        redisAsyncSetDisconnectCallback(_redis_context, Redis_Disconnect_Cb);
        redisAsyncDisconnect(_redis_context);
    }

    void set_redis_ctx(redisAsyncContext *redis_context)
    {
        _redis_context = redis_context;
    }

public:
    Q_SIGNAL void disconnectRedisFailed();
    Q_SIGNAL void disconnectRedisSuccess();

public:
    static void Redis_Connect_Cb(const redisAsyncContext *c, int status)
    {
        Q_UNUSED(c);
        Q_UNUSED(status);
    };

    static void Redis_Disconnect_Cb(const redisAsyncContext *c, int status)
    {
        Q_UNUSED(status);
        auto ctx = static_cast<EventDisconnectRedis*>(c->data);
        ctx->disconnectRedisSuccess();
    };

public:
    std::string _context_errstr;
    redisAsyncContext *_redis_context = nullptr;
};
