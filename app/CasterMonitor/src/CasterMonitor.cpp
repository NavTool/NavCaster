#include <sstream>
#include "CasterMonitor.h"
#include "excute/connectOperate.h"
#include "excute/updateOperate.h"


CasterMonitor::CasterMonitor(QObject *parent) : QObject(parent)
{
    _logger = spdlog::default_logger();

    _caster_mgr->start();
    _auth_mgr->start();

    // ==================== 绑定 EventWorker 到 HashConetxt ====================

    // Auth 相关 → _auth_mgr
    AccountRecords.setWorker(_auth_mgr.get());
    AccountActives.setWorker(_auth_mgr.get());

    // Core 相关 → _caster_mgr
    AccessGroups.setWorker(_caster_mgr.get());
    AccessItems.setWorker(_caster_mgr.get());
    SourceRecords.setWorker(_caster_mgr.get());
    SourceStates.setWorker(_caster_mgr.get());
    ClientStates.setWorker(_caster_mgr.get());
    StreamStates.setWorker(_caster_mgr.get());
    AliasRules.setWorker(_caster_mgr.get());
    PullRecords.setWorker(_caster_mgr.get());
    PullStates.setWorker(_caster_mgr.get());
    PushRecords.setWorker(_caster_mgr.get());
    PushStates.setWorker(_caster_mgr.get());

    // Service 相关 → _caster_mgr
    CasterNodes.setWorker(_caster_mgr.get());

    // ==================== 注册 HashConetxt 回调 ====================
    AccountRecords.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onAccountRecordsUpdated(t, uid, ok, info); });
    AccountActives.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onAccountActivesUpdated(t, uid, ok, info); });
    AccessGroups.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onAccessGroupsUpdated(t, uid, ok, info); });
    AccessItems.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onAccessItemsUpdated(t, uid, ok, info); });
    SourceRecords.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onSourceRecordsUpdated(t, uid, ok, info); });
    SourceStates.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onSourceStatesUpdated(t, uid, ok, info); });
    ClientStates.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onClientStatesUpdated(t, uid, ok, info); });
    StreamStates.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onStreamStatesUpdated(t, uid, ok, info); });
    AliasRules.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onAliasRulesUpdated(t, uid, ok, info); });
    PullRecords.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onPullRecordsUpdated(t, uid, ok, info); });
    PullStates.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onPullStatesUpdated(t, uid, ok, info); });
    PushRecords.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onPushRecordsUpdated(t, uid, ok, info); });
    PushStates.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onPushStatesUpdated(t, uid, ok, info); });
    CasterNodes.setNoticeHashOperateFinishedHandler(
        [this](HashOperateType t, QString uid, bool ok, QVariantMap info) { onCasterNodesUpdated(t, uid, ok, info); });
}


CasterMonitor *CasterMonitor::create(QQmlEngine *, QJSEngine *) {
    return getInstance();
}

QVariantMap CasterMonitor::getNtripServerInfo(QString UID)
{
    auto ptr = m_ntrip_servers.getObjectPtr(UID);
    if (!ptr)
    {
        ntrip_server obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(ptr->info());
}

QVariantMap CasterMonitor::getNtripClientInfo(QString UID)
{
    auto ptr = m_ntrip_clients.getObjectPtr(UID);
    if (!ptr)
    {
        ntrip_client obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(ptr->info());
}

QVariantMap CasterMonitor::getUserAccountInfo(QString UID)
{
    auto ptr = m_user_accounts.getObjectPtr(UID);
    if (!ptr)
    {
        user_account obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(ptr->info());
}

QVariantMap CasterMonitor::getRelayPullInfo(QString UID)
{
    auto ptr = m_relay_pull_items.getObjectPtr(UID);
    if (!ptr)
    {
        relay_pull_item obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(ptr->info());
}

QVariantMap CasterMonitor::getRelayPushInfo(QString UID)
{
    auto ptr = m_relay_push_items.getObjectPtr(UID);
    if (!ptr)
    {
        relay_push_item obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(ptr->info());
}

QVariantMap CasterMonitor::genConnectCasterTemp()
{
    QVariantMap item;
    item["type"] = "ConnectCaster_Op";

    item["solution_UID"] = "";
    item["output_path"] = "";
    item["output_format"] = 0;

    return item;
}

QString CasterMonitor::addConnectCasterOperate(QVariantMap connect_info)
{
    if(_caster_connected)
    {
        noticeError("Caster Network is already in a connected state!");
        return "";
    }

    // 创建一个处理任务
    auto op = std::make_shared<EventConnectRedis>();

    // 设置参数
    op->ip(connect_info["ip"].toString());
    op->port(connect_info["port"].toInt());
    op->auth(connect_info["auth"].toString());


    // // 添加事件保存到上下文
    // _event_map.insert(std::pair(id,op));
    // 连接op的信号到CasterMonitor的槽函数
    connect(op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateCasterRedisCtx,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectCasterSuccess,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectCasterFailed,Qt::UniqueConnection); //,Qt::QueuedConnection);

    auto UID = generate_UniqueKey();
    _caster_event_map.insert(std::pair(UID, op));

    return UID;
}

QString CasterMonitor::addDisconnectCasterOperate()
{
    if(!_caster_connected)
    {
        noticeError("Caster Network is already in a disconnected state!");
        return "";
    }

    auto UID = generate_UniqueKey();

    auto op = std::make_shared<EventDisconnectRedis>();
    op->id(UID);
    op->set_redis_ctx(_caster_mgr->redisCtx());

    auto id= _caster_mgr->postTask(op);
    _caster_event_map.insert(std::pair(id,op));
    return id;
}

QVariantMap CasterMonitor::genConnectAuthTemp()
{
    QVariantMap item;
    item["type"] = "ConnectAuth_Op";

    item["solution_UID"] = "";
    item["output_path"] = "";
    item["output_format"] = 0;

    return item;
}

QString CasterMonitor::addConnectAuthOperate(QVariantMap connect_info)
{
    if(_auth_connected)
    {
        noticeError("Auth Network is already in a connected state!");
        return "";
    }

    // 创建一个处理任务
    auto op = std::make_shared<EventConnectRedis>();

    // 设置参数
    op->ip(connect_info["ip"].toString());
    op->port(connect_info["port"].toInt());
    op->auth(connect_info["auth"].toString());


    // // 添加事件保存到上下文
    // _event_map.insert(std::pair(id,op));
    // 连接op的信号到CasterMonitor的槽函数
    connect(op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateAuthRedisCtx,Qt::UniqueConnection);
    connect(op.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectAuthSuccess,Qt::UniqueConnection);
    connect(op.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectAuthFailed,Qt::UniqueConnection);


    auto UID = generate_UniqueKey();
    _auth_event_map.insert(std::pair(UID, op));

    return UID;
}

QString CasterMonitor::addDisconnectAuthOperate()
{
    if(!_auth_connected)
    {
        noticeError("Auth Network is already in a disconnected state!");
        return "";
    }

    auto UID = generate_UniqueKey();

    auto op = std::make_shared<EventDisconnectRedis>();
    op->id(UID);
    op->set_redis_ctx(_auth_mgr->redisCtx());

    auto id= _auth_mgr->postTask(op);
    _auth_event_map.insert(std::pair(id,op));
    return id;
}

QString CasterMonitor::addRefreshNodeOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateNodeData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateServerData::operateFinished,this,&CasterMonitor::onUpdateNodeMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addRefreshServerOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateServerData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateServerData::operateFinished,this,&CasterMonitor::onUpdataServerMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addRefreshClientOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateClientData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateClientData::operateFinished,this,&CasterMonitor::onUpdataClientMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addRefreshAccountOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateAccountData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateAccountData::operateFinished,this,&CasterMonitor::onUpdateAccountMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addRefreshRelayPullOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateRelayPullData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateRelayPullData::updateListFinished,this,&CasterMonitor::onUpdatePullListMap);
    connect(op.get(),&EventUpdateRelayPullData::updateStatFinished,this,&CasterMonitor::onUpdatePullStatMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addRefreshRelayPushOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateRelayPushData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateRelayPushData::updateListFinished,this,&CasterMonitor::onUpdatePushListMap);
    connect(op.get(),&EventUpdateRelayPushData::updateStatFinished,this,&CasterMonitor::onUpdatePushStatMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addRefreshAlisaRuleOperate()
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventUpdateAliasRuleData>();

    // 设置对象属性
    op->id(UID);

    // 连接信号和槽
    connect(op.get(),&EventUpdateAliasRuleData::operateFinished,this,&CasterMonitor::onUpdateAliasMap);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}


QString CasterMonitor::excuteOperate(QString op_uid)
{
    if(_caster_event_map.find(op_uid)!=_caster_event_map.end())
    {
        return _caster_mgr->postTask(_caster_event_map.find(op_uid)->second);
    }
    if(_caster_redis_map.find(op_uid)!=_caster_redis_map.end())
    {
        return _caster_mgr->postRedisTask(_caster_redis_map.find(op_uid)->second);
    }
    if(_auth_event_map.find(op_uid)!=_auth_event_map.end())
    {
        return _auth_mgr->postTask(_auth_event_map.find(op_uid)->second);
    }
    if(_auth_redis_map.find(op_uid)!=_auth_redis_map.end())
    {
        return _auth_mgr->postRedisTask(_auth_redis_map.find(op_uid)->second);
    }

    return QString();
}

void CasterMonitor::onConnectCasterSuccess()
{
    emit connectCasterSuccess();
}

void CasterMonitor::onConnectCasterFailed()
{
    emit connectCasterFailed();
}

void CasterMonitor::onConnectAuthSuccess()
{
    emit connectAuthSuccess();
}

void CasterMonitor::onConnectAuthFailed()
{
    emit connectAuthFailed();
}

void CasterMonitor::onUpdateCasterRedisCtx(redisAsyncContext *ctx)
{
    // 更新
    _caster_mgr->setRedisCtx(ctx);

    // 启动定时刷新数据

    // 刷新在线基站列表
    // 刷新基站订阅列表

}

void CasterMonitor::onUpdateAuthRedisCtx(redisAsyncContext *ctx)
{
    _auth_mgr->setRedisCtx(ctx);
}

void CasterMonitor::onUpdateNodeMap(QString OP_UID, bool success, QVariantMap info)
{
    m_caster_nodes.syncFromRedis(info);
    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdataServerMap(QString OP_UID, bool success, QVariantMap info)
{
    m_ntrip_servers.syncFromRedis(info);

    // 额外维护挂载点-Connect_key映射表
    _ntrip_serverUID_map.clear();
    m_ntrip_servers.forEach([this](const QString &key, const std::shared_ptr<ntrip_server> &obj) {
        QString alias_mpt = QString::fromStdString(obj->alias_mpt());
        _ntrip_serverUID_map[alias_mpt] = key;
    });

    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdataClientMap(QString OP_UID, bool success, QVariantMap info)
{
    m_ntrip_clients.syncFromRedis(info);

    // 补充计算与基站的距离
    m_ntrip_clients.forEach([this](const QString &key, const std::shared_ptr<ntrip_client> &client) {
        auto server = get_ntrip_server_by_mpt(QString::fromStdString(client->alias_mpt()));
        if (server->position_update_time() != 0 && client->position_update_time() != 0)
        {
            client->distance(util_dist3d(
                server->ecef_x(), server->ecef_y(), server->ecef_z(),
                client->ecef_x(), client->ecef_y(), client->ecef_z()));
        }
    });

    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdateAccountMap(QString OP_UID, bool success, QVariantMap info)
{
    m_user_accounts.syncFromRedis(info);
    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdatePullListMap(QString OP_UID, bool success, QVariantMap info)
{
    m_relay_pull_items.syncFromRedis(info);
    // emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdatePullStatMap(QString OP_UID, bool success, QVariantMap info)
{
    m_relay_pull_stats.syncFromRedis(info);
    emit operateFinished(OP_UID,success,info);
}


void CasterMonitor::onUpdatePushListMap(QString OP_UID, bool success, QVariantMap info)
{
    m_relay_push_items.syncFromRedis(info);
    // emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdatePushStatMap(QString OP_UID, bool success, QVariantMap info)
{
    m_relay_push_stats.syncFromRedis(info);
    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdateAliasMap(QString OP_UID, bool success, QVariantMap info)
{
    m_alias_rules.syncFromRedis(info);
    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onOperateFinished(QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onTimeout()
{

}

std::shared_ptr<ntrip_server> CasterMonitor::get_ntrip_server_by_mpt(QString Mpt)
{
    auto map_item= _ntrip_serverUID_map.find(Mpt);
    if(map_item==_ntrip_serverUID_map.end())
    {
        return std::make_shared<ntrip_server>();
    }

    auto ptr = m_ntrip_servers.getObjectPtr(map_item->second);
    return ptr ? ptr : std::make_shared<ntrip_server>();
}

void CasterMonitor::onAccountRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onAccountActivesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onAccessGroupsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onAccessItemsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onSourceRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onSourceStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onClientStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onStreamStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onAliasRulesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onPullRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onPullStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onPushRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onPushStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

void CasterMonitor::onCasterNodesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{

}

QString CasterMonitor::generate_UniqueKey(int key_length)
{
    std::string new_key;

    // 持续生成新的键直到确保唯一
    do
    {
        new_key = generate_random_key(key_length);
    } while (_generated_key.find(new_key) != _generated_key.end());

    // 将新生成的键加入已使用集合
    _generated_key.insert(new_key);

    // qDebug() << new_key;

    return QString(new_key.c_str());
}

std::string CasterMonitor::generate_random_key(int length)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15); // 生成十六进制数

    // // 获取时间戳和线程ID作为一部分
    // auto time_now = std::chrono::steady_clock::now().time_since_epoch().count();
    // auto thread_id = std::this_thread::get_id();

    std::ostringstream oss;
    // // 使用时间戳和线程ID增加唯一性
    // oss << std::hex <<thread_id << "-" <<time_now;

    // 随机生成附加的16进制字符，增加随机性
    for (size_t i = oss.str().size(); i < length; ++i)
    { // 保证生成指定长度的key
        oss << std::hex << dis(gen);
    }
    return oss.str();
}
