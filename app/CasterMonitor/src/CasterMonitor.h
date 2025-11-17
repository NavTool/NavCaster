#pragma once
#include <QObject>
#include <QtQml/qqml.h>

#include "EventOperationBase.h"
#include "EventWorker.h"
#include "stdafx.h"
#include "spdlog/spdlog.h"


class CasterMonitor : public QObject
{
    Q_OBJECT
    // Q_PROPERTY_AUTO(QVariantMap, project_info) // 站点信息
    QML_SINGLETON
    QML_ELEMENT
private:
    explicit CasterMonitor(QObject *parent = nullptr);

public:
    SINGLETON(CasterMonitor)

    static CasterMonitor *create(QQmlEngine *, QJSEngine *);

public:

    Q_INVOKABLE bool init_Caster_Connect(QVariantMap connect_info);
    Q_INVOKABLE bool init_Auth_Connect(QVariantMap connect_info);
    Q_INVOKABLE bool close_Caster_Connect();
    Q_INVOKABLE bool close_Auth_Connect();

    Q_INVOKABLE bool excute_caster_event(std::shared_ptr<EventOperationBase> op);
    Q_INVOKABLE bool excute_caster_redis(std::shared_ptr<RedisOperationBase> op);
    Q_INVOKABLE bool excute_auth_event(std::shared_ptr<EventOperationBase> op);
    Q_INVOKABLE bool excute_auth_redis(std::shared_ptr<RedisOperationBase> op);

public:
    Q_SIGNAL void connectCasterSuccess();    // 连接成功
    Q_SIGNAL void connectCasterFailed();     // 连接失败
    Q_SIGNAL void reconnectCaster();         // 重连
    Q_SIGNAL void disconnectCaster();        // 断开连接

    Q_SIGNAL void connectAuthSuccess();     // 连接成功
    Q_SIGNAL void connectAuthFailed();      // 连接失败
    Q_SIGNAL void reconnectAuth();          // 重连
    Q_SIGNAL void disconnectAuth();         // 断开连接

private slots:
    void onUpdateCasterRedisCtx(redisAsyncContext *ctx);    // 用于处理连接完成
    void onUpdateAuthRedisCtx(redisAsyncContext *ctx);      // 用于处理连接完成

public:

    std::shared_ptr<spdlog::logger> _logger; // 模块日志器

    std::shared_ptr<EventWorker> _caster_mgr = std::make_shared<EventWorker>();    // CasterCore事件管理
    std::shared_ptr<EventWorker> _auth_mgr = std::make_shared<EventWorker>();      // AuthVerify事件管理


    std::map<uint64_t,std::shared_ptr<EventOperationBase>>  _event_map;
    std::map<uint64_t,std::shared_ptr<RedisOperationBase>>  _redis_map;


};

