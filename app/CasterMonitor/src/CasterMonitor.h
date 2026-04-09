#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <set>
#include "util.h"

#include "EventOperationBase.h"
#include "EventWorker.h"

#include "template/HashOperate.h"
#include "template/Context.h"

#include "context/user_account.h"
#include "context/caster_node.h"
#include "context/ntrip_client.h"
#include "context/ntrip_server.h"
#include "context/relay_pull.h"
#include "context/relay_push.h"
#include "context/alias_rule.h"
#include "stdafx.h"
#include "spdlog/spdlog.h"


#include "auth/AccountActive.pb.h"
#include "auth/AccountRecord.pb.h"

#include "core/AccessGroup.pb.h"
#include "core/AccessItem.pb.h"
#include "core/AliasRule.pb.h"
#include "core/BroadcastMsg.pb.h"
#include "core/ClientState.pb.h"
#include "core/PullRecord.pb.h"
#include "core/PullState.pb.h"
#include "core/PushRecord.pb.h"
#include "core/PushState.pb.h"
#include "core/ServerState.pb.h"
#include "core/SourceRecord.pb.h"
#include "core/StreamState.pb.h"


#include "service/CasterNode.pb.h"


using namespace caster::auth;
using namespace caster::core;
using namespace caster::service;



#define CONCAT2(a, b) a##b
#define CONCAT3(a, b, c) a##b##c


// ============================================================================
//  Q_HASH_CRUD_API —— 为 HashConetxt 成员自动生成 QML 可调用的完整 CRUD 接口
//
//  NAME   : 实体名称（如 AccountRecord），用于拼接函数名
//  MEMBER : CasterMonitor 中对应的 HashConetxt 成员变量名
//
//  生成的 QML 接口：
//      generateXxxTemp()          → 返回 Proto 默认值的 QVariantMap 模板
//      addXxx(field, info)        → HSETNX，返回 OP_UID
//      delXxx(field)              → HDEL，  返回 OP_UID
//      setXxx(field, info)        → HSET，  返回 OP_UID
//      fetchXxx(field)            → HGET（异步），返回 OP_UID
//      refreshAllXxx()            → HGETALL（异步），返回 OP_UID
//      getXxx(field)              → 本地缓存同步查询，返回 QVariantMap
//      getAllXxx()                → 本地缓存同步查询全部，返回 QVariantMap
//
//  使用示例（QML）：
//      var temp = CasterMonitor.generateAccountRecordTemp()
//      temp.account = "user001"
//      var opId = CasterMonitor.addAccountRecord("user001", temp)
//      // 监听 operateFinished(opId, success, info) 获取结果
// ============================================================================
#define Q_HASH_CRUD_API(NAME, MEMBER)                                                              \
    Q_INVOKABLE QVariantMap CONCAT3(generate, NAME, Temp)()                                        \
    {                                                                                               \
        return MEMBER.generateTemplate();                                                           \
    }                                                                                               \
    Q_INVOKABLE QString CONCAT2(add, NAME)(const QString &field, const QVariantMap &info)           \
    {                                                                                               \
        return MEMBER.addItem(field, info);                                                         \
    }                                                                                               \
    Q_INVOKABLE QString CONCAT2(del, NAME)(const QString &field)                                   \
    {                                                                                               \
        return MEMBER.delItem(field);                                                               \
    }                                                                                               \
    Q_INVOKABLE QString CONCAT2(set, NAME)(const QString &field, const QVariantMap &info)           \
    {                                                                                               \
        return MEMBER.setItem(field, info);                                                         \
    }                                                                                               \
    Q_INVOKABLE QString CONCAT2(fetch, NAME)(const QString &field)                                 \
    {                                                                                               \
        return MEMBER.fetchItem(field);                                                             \
    }                                                                                               \
    Q_INVOKABLE QString CONCAT2(refreshAll, NAME)()                                                \
    {                                                                                               \
        return MEMBER.refreshAll();                                                                 \
    }                                                                                               \
    Q_INVOKABLE QVariantMap CONCAT2(get, NAME)(const QString &field)                               \
    {                                                                                               \
        return MEMBER.getItemInfo(field);                                                           \
    }                                                                                               \
    Q_INVOKABLE QVariantMap CONCAT2(getAll, NAME)()                                                \
    {                                                                                               \
        return MEMBER.getAllItemInfo();                                                              \
    }



class CasterMonitor : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT
private:
    explicit CasterMonitor(QObject *parent = nullptr);

public:
    SINGLETON(CasterMonitor)
    static CasterMonitor *create(QQmlEngine *, QJSEngine *);

public:
    // 查询函数（本地缓存，兼容旧接口）
    Q_INVOKABLE QVariantMap getNtripServerInfo(QString UID);
    Q_INVOKABLE QVariantMap getNtripClientInfo(QString UID);
    Q_INVOKABLE QVariantMap getUserAccountInfo(QString UID);
    Q_INVOKABLE QVariantMap getRelayPullInfo(QString UID);
    Q_INVOKABLE QVariantMap getRelayPushInfo(QString UID);

    // 全量刷新数据（旧事件系统，保留兼容）
    Q_INVOKABLE QString addRefreshNodeOperate();
    Q_INVOKABLE QString addRefreshServerOperate();
    Q_INVOKABLE QString addRefreshClientOperate();
    Q_INVOKABLE QString addRefreshAccountOperate();
    Q_INVOKABLE QString addRefreshRelayPullOperate();
    Q_INVOKABLE QString addRefreshRelayPushOperate();
    Q_INVOKABLE QString addRefreshAlisaRuleOperate();

public:
    // ==================== 连接管理 ====================

    // 连接Caster
    Q_INVOKABLE QVariantMap genConnectCasterTemp();
    Q_INVOKABLE QString addConnectCasterOperate(QVariantMap connect_info);
    Q_INVOKABLE QString addDisconnectCasterOperate();

    // 连接Auth
    Q_INVOKABLE QVariantMap genConnectAuthTemp();
    Q_INVOKABLE QString addConnectAuthOperate(QVariantMap connect_info);
    Q_INVOKABLE QString addDisconnectAuthOperate();

    // ==================== 旧事件任务执行 ====================

    Q_INVOKABLE QString excuteOperate(QString op_uid);  // 执行指令

public:
    // 通用执行操作通知(这些
    Q_SIGNAL void noticeSuccess(QString msg);
    Q_SIGNAL void noticeInfo(QString msg);
    Q_SIGNAL void noticeWarning(QString msg);
    Q_SIGNAL void noticeError(QString msg);

    Q_SIGNAL void connectCasterSuccess(); // 连接成功
    Q_SIGNAL void connectCasterFailed();  // 连接失败
    Q_SIGNAL void reconnectCaster();      // 重连
    Q_SIGNAL void disconnectCaster();     // 断开连接

    Q_SIGNAL void connectAuthSuccess(); // 连接成功
    Q_SIGNAL void connectAuthFailed();  // 连接失败
    Q_SIGNAL void reconnectAuth();      // 重连
    Q_SIGNAL void disconnectAuth();     // 断开连接

    // 操作执行结果信号
    Q_SIGNAL void operateFinished(QString OP_UID, bool success, QVariantMap info);

private slots:
    void onConnectCasterSuccess();                       // 用于处理连接完成
    void onConnectCasterFailed();                        // 用于处理连接完成
    void onUpdateCasterRedisCtx(redisAsyncContext *ctx); // 用于处理连接完成

    void onConnectAuthSuccess();                       // 用于处理连接完成
    void onConnectAuthFailed();                        // 用于处理连接完成
    void onUpdateAuthRedisCtx(redisAsyncContext *ctx); // 用于处理连接完成

    void onUpdateNodeMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdataServerMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdataClientMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdateAccountMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdatePullListMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdatePullStatMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdatePushListMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdatePushStatMap(QString OP_UID, bool success, QVariantMap info);
    void onUpdateAliasMap(QString OP_UID, bool success, QVariantMap info);

    // 任务操作发送的信号通过这个转发
    void onOperateFinished(QString OP_UID, bool success, QVariantMap info);

    void onTimeout(); // 定时任务执行函数

private:
    std::shared_ptr<ntrip_server> get_ntrip_server_by_mpt(QString Mpt);

public:
    std::shared_ptr<spdlog::logger> _logger; // 模块日志器

    std::shared_ptr<EventWorker> _caster_mgr = std::make_shared<EventWorker>(); // CasterCore事件管理
    std::shared_ptr<EventWorker> _auth_mgr = std::make_shared<EventWorker>();   // AuthVerify事件管理
    std::map<QString, std::shared_ptr<EventOperationBase>> _caster_event_map;
    std::map<QString, std::shared_ptr<RedisOperationBase>> _caster_redis_map;
    std::map<QString, std::shared_ptr<EventOperationBase>> _auth_event_map;
    std::map<QString, std::shared_ptr<RedisOperationBase>> _auth_redis_map;

    bool _caster_connected = false;
    bool _auth_connected = false;

public:
    std::unordered_map<QString, QString> _ntrip_serverUID_map; // 挂载点 → Connect_Key 映射

    // 本地数据上下文（线程安全，带 flag 同步）
    Context<caster_node>    m_caster_nodes;
    Context<ntrip_server>   m_ntrip_servers;
    Context<ntrip_client>   m_ntrip_clients;
    Context<user_account>   m_user_accounts;
    Context<relay_pull_item> m_relay_pull_items;
    Context<relay_push_item> m_relay_push_items;
    Context<relay_pull_stat> m_relay_pull_stats;
    Context<relay_push_stat> m_relay_push_stats;
    Context<alias_rule>     m_alias_rules;

public:


    // Redis同步数据和操作(auth）
    static constexpr char AccountRecordTableName[] = "ACT:RECORD";
    static constexpr char AccountActiveTableName[] = "STR:ACTIVE";

    HashConetxt<AccountRecord,AccountRecordTableName>  AccountRecords;
    HashConetxt<AccountActive,AccountActiveTableName>  AccountActives;

    // Redis同步数据和操作（core）
    static constexpr char AccessGroupTableName[] = "ACCESS:GROUP";
    static constexpr char AccessItemTableName[]  = "ACCESS:ACCESS:XXX";
    static constexpr char SourceRecordTableName[] = "MPT:RECORD";
    static constexpr char ServerStateTableName[]  = "MPT:STAT";
    static constexpr char ClientStateTableName[]  = "USR:STAT";
    static constexpr char StreamStateTableName[]  = "STR:STAT";
    static constexpr char AliasRuleTableName[]   = "STR:ALIAS:LIST";
    static constexpr char PullRecordsTableName[] = "STR:PULL:LIST";
    static constexpr char PullStatesTableName[]  = "STR:PULL:STAT";
    static constexpr char PushRecordsTableName[] = "STR:PUSH:LIST";
    static constexpr char PushStatesTableName[]  = "STR:PUSH:STAT";

    HashConetxt<AccessGroup,AccessGroupTableName>  AccessGroups;
    HashConetxt<AccessItem,AccessItemTableName>  AccessItems;
    HashConetxt<SourceRecord,SourceRecordTableName>  SourceRecords;
    HashConetxt<ServerState,ServerStateTableName>  SourceStates;
    HashConetxt<ClientState,ClientStateTableName>  ClientStates;
    HashConetxt<StreamState,StreamStateTableName>  StreamStates;
    HashConetxt<AliasRule,AliasRuleTableName>  AliasRules;
    HashConetxt<PullRecord,PullRecordsTableName>  PullRecords;
    HashConetxt<PullState,PullStatesTableName>  PullStates;
    HashConetxt<PushRecord,PushRecordsTableName>  PushRecords;
    HashConetxt<PushState,PushStatesTableName>  PushStates;

    // Redis同步数据和操作（service）
    static constexpr char CasterNodeTableName[] = "CASTER:NODE";

    HashConetxt<CasterNode,CasterNodeTableName>  CasterNodes;




public:

    // ==================== Redis HASH 自动生成的 CRUD 接口 ====================

    // Auth 相关
    Q_HASH_CRUD_API(AccountRecord, AccountRecords)
    Q_HASH_CRUD_API(AccountActive, AccountActives)

    // Core 相关
    Q_HASH_CRUD_API(AccessGroup, AccessGroups)
    Q_HASH_CRUD_API(AccessItem, AccessItems)
    Q_HASH_CRUD_API(SourceRecord, SourceRecords)
    Q_HASH_CRUD_API(ServerState, SourceStates)
    Q_HASH_CRUD_API(ClientState, ClientStates)
    Q_HASH_CRUD_API(StreamState, StreamStates)
    Q_HASH_CRUD_API(AliasRule, AliasRules)
    Q_HASH_CRUD_API(PullRecord, PullRecords)
    Q_HASH_CRUD_API(PullState, PullStates)
    Q_HASH_CRUD_API(PushRecord, PushRecords)
    Q_HASH_CRUD_API(PushState, PushStates)

    // Service 相关
    Q_HASH_CRUD_API(CasterNode, CasterNodes)

private slots:
    // redis异步更新后，更新内部的上下文
    void onAccountRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onAccountActivesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);

    void onAccessGroupsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onAccessItemsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onSourceRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onSourceStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onClientStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onStreamStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onAliasRulesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);

    void onPullRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onPullStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onPushRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onPushStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);
    void onCasterNodesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info);


public:
    QString generate_UniqueKey(int key_length = 8);

private:
    std::string generate_random_key(int length);
    std::set<std::string> _generated_key; // 已经生成过的唯一key值
};
