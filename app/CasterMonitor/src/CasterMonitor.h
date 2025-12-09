#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <set>

#include "EventOperationBase.h"
#include "EventWorker.h"


#include "context/user_account.h"
#include "context/caster_node.h"
#include "context/ntrip_client.h"
#include "context/ntrip_server.h"
#include "context/relay_pull.h"
#include "context/relay_push.h"
#include "context/alias_rule.h"
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

    // 查询函数
    Q_INVOKABLE QVariantMap getNtripServerInfo(QString UID);
    Q_INVOKABLE QVariantMap getNtripServerInfoByMpt(QString Mpt);
    Q_INVOKABLE QVariantMap getNtripClientInfo(QString UID);
    Q_INVOKABLE QVariantMap getUserAccountInfo(QString UID);
    Q_INVOKABLE QVariantMap getRelayPullInfo(QString UID);
    Q_INVOKABLE QVariantMap getRelayPushInfo(QString UID);
    Q_INVOKABLE QVariantMap getAliasRuleInfo(QString UID);


    // 全量刷新数据
    Q_INVOKABLE QString addRefreshNodeOperate();
    Q_INVOKABLE QString addRefreshServerOperate();
    Q_INVOKABLE QString addRefreshClientOperate();
    Q_INVOKABLE QString addRefreshAccountOperate();
    Q_INVOKABLE QString addRefreshRelayPullOperate();
    Q_INVOKABLE QString addRefreshRelayPushOperate();
    Q_INVOKABLE QString addRefreshAlisaRuleOperate();


public:

    // 执行指令->创建指令对象（生成任务ID,返回给命令创建者），存储到map中
    // 执行任务，将传递到任务队列->任务队列执行->更新context上下文->通知任务执行完成（成功/失败）->清理指令对象（或者不清理,下次继续执行）
    // 任务创建者拿到命令ID，监听命令ID，根据反馈执行对应的操作

    // 创建任务

    //连接Caster
    Q_INVOKABLE QVariantMap genConnectCasterTemp();
    Q_INVOKABLE QString addConnectCasterOperate(QVariantMap connect_info);
    Q_INVOKABLE QString addDisconnectCasterOperate();

    //连接Auth
    Q_INVOKABLE QVariantMap genConnectAuthTemp();
    Q_INVOKABLE QString addConnectAuthOperate(QVariantMap connect_info);
    Q_INVOKABLE QString addDisconnectAuthOperate();


    // 账号管理
    Q_INVOKABLE QVariantMap genAccountTemp();
    Q_INVOKABLE QString addAddAccountOperate(QVariantMap account_info);  // 添加账号（远程操作，添加完成后，本地也同步更新）
    Q_INVOKABLE QString addSetAccountOperate(QVariantMap account_info);  // 修改已有账号信息（远程操作，添加完成后，本地也同步更新）
    Q_INVOKABLE QString addDelAccountOperate(QVariantMap account_info);  // 删除账号（添加完成后，本地也同步更新）
    Q_INVOKABLE QString addGetAccountOperate(QVariantMap account_info);  // 查询账号（远程操作）


    // 数据接入任务
    Q_INVOKABLE QVariantMap genPullStreamTemp();
    Q_INVOKABLE QString addAddPullStreamOperate(QVariantMap relay_info);  // 添加
    Q_INVOKABLE QString addSetPullStreamOperate(QVariantMap relay_info);  // 修改
    Q_INVOKABLE QString addDelPullStreamOperate(QVariantMap relay_info);  // 删除
    Q_INVOKABLE QString addGetPullStreamOperate(QVariantMap relay_info);  // 查询

    // 数据推送任务
    Q_INVOKABLE QVariantMap genPushStreamTemp();
    Q_INVOKABLE QString addAddPushStreamOperate(QVariantMap relay_info);  // 添加
    Q_INVOKABLE QString addSetPushStreamOperate(QVariantMap relay_info);  // 修改
    Q_INVOKABLE QString addDelPushStreamOperate(QVariantMap relay_info);  // 删除
    Q_INVOKABLE QString addGetPushStreamOperate(QVariantMap relay_info);  // 查询

    // 数据流别名
    Q_INVOKABLE QVariantMap genAliasRuleTemp();
    Q_INVOKABLE QString addAddAliasRuleOperate(QVariantMap alias_info);  // 添加
    Q_INVOKABLE QString addSetAliasRuleOperate(QVariantMap alias_info);  // 修改
    Q_INVOKABLE QString addDelAliasRuleOperate(QVariantMap alias_info);  // 删除
    Q_INVOKABLE QString addGetAliasRuleOperate(QVariantMap alias_info);  // 查询


    // 执行任务
    Q_INVOKABLE QString excuteOperate(QString op_uid);   // 执行指令
    // 取消执行
    Q_INVOKABLE QString cancelOperate(QString op_uid);   // 取消指令
    // 清理任务
    Q_INVOKABLE QString deleteOperate(QString op_uid);   // 删除指令

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

    // 操作执行结果信号
    Q_SIGNAL void operateFinished(QString OP_UID,bool success,QVariantMap info);

private slots:
    void onConnectCasterSuccess();    // 用于处理连接完成
    void onConnectCasterFailed();    // 用于处理连接完成
    void onUpdateCasterRedisCtx(redisAsyncContext *ctx);    // 用于处理连接完成

    void onConnectAuthSuccess();    // 用于处理连接完成
    void onConnectAuthFailed();    // 用于处理连接完成
    void onUpdateAuthRedisCtx(redisAsyncContext *ctx);      // 用于处理连接完成


    void onUpdateNodeMap(QString OP_UID,bool success,QVariantMap info);
    void onUpdataServerMap(QString OP_UID,bool success,QVariantMap info);
    void onUpdataClientMap(QString OP_UID,bool success,QVariantMap info);
    void onUpdateAccountMap(QString OP_UID,bool success,QVariantMap info);
    void onUpdatePullMap(QString OP_UID,bool success,QVariantMap info);
    void onUpdatePushMap(QString OP_UID,bool success,QVariantMap info);
    void onUpdateAliasMap(QString OP_UID,bool success,QVariantMap info);

    //任务操作发送的信号通过这个转发
    void onOperateFinished(QString OP_UID,bool success,QVariantMap info);

    void onTimeout();    // 定时任务执行函数

private:

    std::shared_ptr<ntrip_server>  get_ntrip_server_by_mpt(QString Mpt);



public:

    std::shared_ptr<spdlog::logger> _logger; // 模块日志器

    std::shared_ptr<EventWorker> _caster_mgr = std::make_shared<EventWorker>();    // CasterCore事件管理
    std::shared_ptr<EventWorker> _auth_mgr   = std::make_shared<EventWorker>();    // AuthVerify事件管理
    std::map<QString,std::shared_ptr<EventOperationBase>>  _caster_event_map;
    std::map<QString,std::shared_ptr<RedisOperationBase>>  _caster_redis_map;
    std::map<QString,std::shared_ptr<EventOperationBase>>  _auth_event_map;
    std::map<QString,std::shared_ptr<RedisOperationBase>>  _auth_redis_map;

    bool _caster_connected=false;
    bool _auth_connected=false;

public:

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
    // std::set<QString> _active_ntrip_server_set;   // 在线挂载点  MPT:STAT
    // std::set<QString> _active_ntrip_client_set;   // 在线用户    USR:STAT
    std::unordered_map<QString,QString> _ntrip_serverUID_map;   // Connect_Key - 挂载点 MPT:SRV  // 根据挂载点查找到Connect_Key
    // std::unordered_map<QString,QString> _ntrip_clientUID_map;   // Connect_Key - 用户名 USR:SRV  // 根据用户名查找到Connect_Key

    // Caster资源
    std::unordered_map<QString, std::shared_ptr<caster_node>> m_caster_node_map;

    std::unordered_map<QString, std::shared_ptr<ntrip_server>> m_ntrip_server_map;    // Connect_Key，对象，站点的基本信息
    std::unordered_map<QString, std::shared_ptr<ntrip_client>> m_ntrip_client_map;    // Connect_Key，对象，站点的基本信息
    std::unordered_map<QString, std::shared_ptr<user_account>> m_user_account_map;          // key，对象，站点的基本信息

    std::unordered_map<QString, std::shared_ptr<relay_pull>> m_relay_pull_map;          // key，对象，站点的基本信息
    std::unordered_map<QString, std::shared_ptr<relay_push>> m_relay_push_map;          // key，对象，站点的基本信息
    std::unordered_map<QString, std::shared_ptr<alias_rule>> m_alias_rule_map;          // key，对象，站点的基本信息

public:



public:
    QString generate_UniqueKey(int key_length = 8);
private:
    std::string generate_random_key(int length);
    std::set<std::string> _generated_key; // 已经生成过的唯一key值

};

