#pragma once
#include <QObject>
#include <QVariant>
#include <async.h>
#include <event2/event.h>
#include <adapters/libevent.h>
#include "stdafx.h"
/**
 * 基础 Event 异步操作类
 */
class EventOperationBase : public QObject {
    Q_OBJECT
    Q_PROPERTY_AUTO(QString, id)
    Q_PROPERTY_AUTO(int, type)
public:
    explicit EventOperationBase(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~EventOperationBase() {}

    // 操作名称（可用于日志、调试）
    virtual QString name() const = 0;

    // 1️执行函数：发起 libevent 相关操作
    virtual void execute(event_base* base) = 0;

signals:
    // 2️操作完成通知 Qt 主线程
    void operateFinished(QString OP_UID,bool success,QVariantMap info);
};



/**
 * 基础 Redis 异步操作类
 */
class RedisOperationBase : public QObject {
    Q_OBJECT
    Q_PROPERTY_AUTO(QString, id)
public:
    explicit RedisOperationBase(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~RedisOperationBase() {}

    // 操作名称（可用于日志、调试）
    virtual QString name() const = 0;

    // 1执行函数：发起 Redis 命令
    virtual void execute(redisAsyncContext *ctx) = 0;

signals:
    // 3️操作完成通知 Qt 主线程
    void operateFinished(QString OP_UID,bool success,QVariantMap info);
};
