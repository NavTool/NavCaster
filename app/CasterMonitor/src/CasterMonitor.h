#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <set>

#include "EventOperationBase.h"
#include "EventWorker.h"
#include "connectRedis.h"

#include "context/auth_user.h"
#include "context/caster_node.h"
#include "context/ntrip_client.h"
#include "context/ntrip_server.h"
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
    //通用执行操作通知(这些
    Q_SIGNAL void noticeSuccess(QString msg);
    Q_SIGNAL void noticeInfo(QString msg);
    Q_SIGNAL void noticeWarning(QString msg);
    Q_SIGNAL void noticeError(QString msg);

    Q_SIGNAL void connectCasterSuccess();    // 连接成功
    Q_SIGNAL void connectCasterFailed();     // 连接失败
    Q_SIGNAL void reconnectCaster();         // 重连
    Q_SIGNAL void disconnectCaster();        // 断开连接

    Q_SIGNAL void connectAuthSuccess();     // 连接成功
    Q_SIGNAL void connectAuthFailed();      // 连接失败
    Q_SIGNAL void reconnectAuth();          // 重连
    Q_SIGNAL void disconnectAuth();         // 断开连接

private slots:
    void onConnectCasterSuccess();    // 用于处理连接完成
    void onConnectCasterFailed();    // 用于处理连接完成
    void onUpdateCasterRedisCtx(redisAsyncContext *ctx);    // 用于处理连接完成

    void onConnectAuthSuccess();    // 用于处理连接完成
    void onConnectAuthFailed();    // 用于处理连接完成
    void onUpdateAuthRedisCtx(redisAsyncContext *ctx);      // 用于处理连接完成

public:

    std::shared_ptr<spdlog::logger> _logger; // 模块日志器

    std::shared_ptr<EventWorker> _caster_mgr = std::make_shared<EventWorker>();    // CasterCore事件管理
    std::shared_ptr<EventWorker> _auth_mgr   = std::make_shared<EventWorker>();    // AuthVerify事件管理
    std::map<uint64_t,std::shared_ptr<EventOperationBase>>  _event_map;
    std::map<uint64_t,std::shared_ptr<RedisOperationBase>>  _redis_map;

    std::shared_ptr<EventConnectRedis> _caster_con = std::make_shared<EventConnectRedis>();  // caster_连接指令
    std::shared_ptr<EventConnectRedis> _auth_con   = std::make_shared<EventConnectRedis>();  // auth_连接指令

private:

    // 更新在线的基线列表（基本信息）   只刷新基本信息，更加详细的信息采用op的方式直接查询

    /*
     *      查询 MPT:LIST     O(1)    获取所有的在线挂载点名称
     *      新增 MPT:SRV      O(1)    获取所有基站连接和挂载点的映射关系
     *      查询 MPT:REC:*    O(n)
     *      查询 MPT:SUB:*    O(n)
     *
     *      查询 USR:LIST     O(1)    获取所有的在线用户名
     *      新增 USR:SRV      O(1)    获取所有用户连接和挂载点的映射关系
     *      查询 USR:REC:*    O(n)
     *      查询 MPT:SUB:*    O(n)
     */


    // 这两个是定期刷新的内容，其他内容都是以这个内容为基础进行刷新
    std::set<std::string> _active_ntrip_server_set;   // 在线挂载点  MPT:LIST
    std::set<std::string> _active_ntrip_client_set;   // 在线用户    USR:LIST
    std::unordered_map<std::string,std::string> _active_ntrip_serverUID_set;   // Connect_Key - 挂载点 MPT:SRV
    std::unordered_map<std::string,std::string> _active_ntrip_clientUID_set;   // Connect_Key - 用户名 USR:SRV

    // Caster资源
    std::unordered_map<std::string, std::shared_ptr<caster_node>> m_caster_node_map;

    std::unordered_map<std::string, std::shared_ptr<ntrip_server>> m_ntrip_server_map;    // Connect_Key，对象，站点的基本信息
    std::unordered_map<std::string, std::shared_ptr<ntrip_client>> m_ntrip_client_map;    // Connect_Key，对象，站点的基本信息

    std::unordered_map<std::string, std::shared_ptr<auth_user>> m_auth_user_map;    // key，对象，站点的基本信息


};

