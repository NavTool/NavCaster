#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <set>
#include "util.h"

#include "EventOperationBase.h"
#include "EventWorker.h"
#include "ConnectOperate.h"

#include "template/HashOperate.h"

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
//  Q_HASH_CRUD_API —— 为 HashContext 成员自动生成 QML 可调用的完整 CRUD 接口
//
//  NAME   : 实体名称（如 AccountRecord），用于拼接函数名
//  MEMBER : CasterMonitor 中对应的 HashContext 成员变量名
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
    // ==================== 连接管理 ====================

    Q_INVOKABLE void connectCaster(const QString &ip, int port, const QString &auth);
    Q_INVOKABLE void disconnectCaster();

    Q_INVOKABLE void connectAuth(const QString &ip, int port, const QString &auth);
    Q_INVOKABLE void disconnectAuth();

public:
    // 通用执行操作通知(这些
    Q_SIGNAL void noticeSuccess(QString msg);
    Q_SIGNAL void noticeInfo(QString msg);
    Q_SIGNAL void noticeWarning(QString msg);
    Q_SIGNAL void noticeError(QString msg);

    Q_SIGNAL void connectCasterSuccess(); // 连接成功
    Q_SIGNAL void connectCasterFailed();  // 连接失败
    Q_SIGNAL void reconnectCaster();      // 重连
    Q_SIGNAL void casterDisconnected();   // 已断开连接

    Q_SIGNAL void connectAuthSuccess(); // 连接成功
    Q_SIGNAL void connectAuthFailed();  // 连接失败
    Q_SIGNAL void reconnectAuth();      // 重连
    Q_SIGNAL void authDisconnected();   // 已断开连接

    // 操作执行结果信号
    Q_SIGNAL void operateFinished(QString OP_UID, bool success, QVariantMap info);

private slots:
    void onConnectCasterSuccess();                       // 用于处理连接完成
    void onConnectCasterFailed();                        // 用于处理连接完成
    void onUpdateCasterRedisCtx(redisAsyncContext *ctx); // 用于处理连接完成

    void onConnectAuthSuccess();                       // 用于处理连接完成
    void onConnectAuthFailed();                        // 用于处理连接完成
    void onUpdateAuthRedisCtx(redisAsyncContext *ctx); // 用于处理连接完成

    void onTimeout(); // 定时任务执行函数

public:
    std::shared_ptr<spdlog::logger> _logger; // 模块日志器

    std::shared_ptr<EventWorker> _caster_mgr = std::make_shared<EventWorker>(); // CasterCore事件管理
    std::shared_ptr<EventWorker> _auth_mgr = std::make_shared<EventWorker>();   // AuthVerify事件管理

    bool _caster_connected = false;
    bool _auth_connected = false;

    // 当前连接操作（保持 shared_ptr 生命周期直至回调完成）
    std::shared_ptr<EventConnectRedis> _caster_connect_op;
    std::shared_ptr<EventConnectRedis> _auth_connect_op;

public:
    std::unordered_map<QString, QString> _ntrip_serverUID_map; // 挂载点 → Connect_Key 映射

public:


    // Redis同步数据和操作(auth）
    static constexpr char AccountRecordTableName[] = "ACT:RECORD";
    static constexpr char AccountActiveTableName[] = "STR:ACTIVE";

    HashContext<AccountRecord,AccountRecordTableName>  AccountRecords;
    HashContext<AccountActive,AccountActiveTableName>  AccountActives;

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

    HashContext<AccessGroup,AccessGroupTableName>  AccessGroups;
    HashContext<AccessItem,AccessItemTableName>  AccessItems;
    HashContext<SourceRecord,SourceRecordTableName>  SourceRecords;
    HashContext<ServerState,ServerStateTableName>  SourceStates;
    HashContext<ClientState,ClientStateTableName>  ClientStates;
    HashContext<StreamState,StreamStateTableName>  StreamStates;
    HashContext<AliasRule,AliasRuleTableName>  AliasRules;
    HashContext<PullRecord,PullRecordsTableName>  PullRecords;
    HashContext<PullState,PullStatesTableName>  PullStates;
    HashContext<PushRecord,PushRecordsTableName>  PushRecords;
    HashContext<PushState,PushStatesTableName>  PushStates;

    // Redis同步数据和操作（service）
    static constexpr char CasterNodeTableName[] = "CASTER:NODE";

    HashContext<CasterNode,CasterNodeTableName>  CasterNodes;




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
