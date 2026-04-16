#include <sstream>
#include "CasterMonitor.h"
#include "knt.h"


CasterMonitor::CasterMonitor(QObject *parent) : QObject(parent)
{
    _logger = spdlog::default_logger();

    _caster_mgr->start();
    _auth_mgr->start();

    // ==================== 绑定 EventWorker 到 HashContext ====================

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

    // ==================== 注册 HashContext 回调 ====================
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

void CasterMonitor::connectCaster(const QString &ip, int port, const QString &auth)
{
    if(_caster_connected)
    {
        noticeError("Caster Network is already in a connected state!");
        return;
    }

    _caster_connect_op = std::make_shared<EventConnectRedis>();
    _caster_connect_op->ip(ip);
    _caster_connect_op->port(port);
    _caster_connect_op->auth(auth);

    connect(_caster_connect_op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateCasterRedisCtx,Qt::UniqueConnection);
    connect(_caster_connect_op.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectCasterSuccess,Qt::UniqueConnection);
    connect(_caster_connect_op.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectCasterFailed,Qt::UniqueConnection);

    _caster_mgr->postTask(_caster_connect_op);
}

void CasterMonitor::disconnectCaster()
{
    if(!_caster_connected)
    {
        noticeError("Caster Network is already in a disconnected state!");
        return;
    }

    auto op = std::make_shared<EventDisconnectRedis>();
    op->set_redis_ctx(_caster_mgr->redisCtx());
    _caster_mgr->postTask(op);
}

void CasterMonitor::connectAuth(const QString &ip, int port, const QString &auth)
{
    if(_auth_connected)
    {
        noticeError("Auth Network is already in a connected state!");
        return;
    }

    _auth_connect_op = std::make_shared<EventConnectRedis>();
    _auth_connect_op->ip(ip);
    _auth_connect_op->port(port);
    _auth_connect_op->auth(auth);

    connect(_auth_connect_op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateAuthRedisCtx,Qt::UniqueConnection);
    connect(_auth_connect_op.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectAuthSuccess,Qt::UniqueConnection);
    connect(_auth_connect_op.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectAuthFailed,Qt::UniqueConnection);

    _auth_mgr->postTask(_auth_connect_op);
}

void CasterMonitor::disconnectAuth()
{
    if(!_auth_connected)
    {
        noticeError("Auth Network is already in a disconnected state!");
        return;
    }

    auto op = std::make_shared<EventDisconnectRedis>();
    op->set_redis_ctx(_auth_mgr->redisCtx());
    _auth_mgr->postTask(op);
}

void CasterMonitor::onConnectCasterSuccess()
{
    // 直接从连接操作对象读取 redisAsyncContext*，
    // 避免依赖 updateRedisCtx(redisAsyncContext*) 的跨线程 QueuedConnection
    // （该信号的指针参数可能因未注册 metatype 而被静默丢弃）
    if (_caster_connect_op && _caster_connect_op->_redis_context) {
        _caster_mgr->setRedisCtx(_caster_connect_op->_redis_context);
    }
    _caster_connected = true;
    emit connectCasterSuccess();
}

void CasterMonitor::onConnectCasterFailed()
{
    emit connectCasterFailed();
}

void CasterMonitor::onConnectAuthSuccess()
{
    if (_auth_connect_op && _auth_connect_op->_redis_context) {
        _auth_mgr->setRedisCtx(_auth_connect_op->_redis_context);
    }
    _auth_connected = true;
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

void CasterMonitor::onTimeout()
{

}

void CasterMonitor::onAccountRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onAccountActivesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onAccessGroupsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onAccessItemsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onSourceRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onSourceStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    // 重建挂载点 → Connect_Key 映射表
    if (success && (type == HashOperateType::GET_ALL || type == HashOperateType::GET))
    {
        _ntrip_serverUID_map.clear();
        SourceStates.forEach([this](const std::string &key, const std::shared_ptr<ServerState> &obj) {
            QString alias_mpt = QString::fromStdString(obj->alias_mpt());
            _ntrip_serverUID_map[alias_mpt] = QString::fromStdString(key);
        });
    }
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onClientStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    // 计算客户端与基站的距离
    if (success && (type == HashOperateType::GET_ALL || type == HashOperateType::GET))
    {
        ClientStates.forEach([this](const std::string &key, const std::shared_ptr<ClientState> &client) {
            auto map_item = _ntrip_serverUID_map.find(QString::fromStdString(client->alias_mpt()));
            if (map_item != _ntrip_serverUID_map.end())
            {
                auto server = SourceStates.getLocalObject(map_item->second.toStdString());
                if (server && server->position_update_time() != 0 && client->position_update_time() != 0)
                {
                    client->set_distance(util_dist3d(
                        server->ecef_x(), server->ecef_y(), server->ecef_z(),
                        client->ecef_x(), client->ecef_y(), client->ecef_z()));
                }
            }
        });
    }
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onStreamStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onAliasRulesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onPullRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onPullStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onPushRecordsUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onPushStatesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
}

void CasterMonitor::onCasterNodesUpdated(HashOperateType type, QString OP_UID, bool success, QVariantMap info)
{
    emit operateFinished(OP_UID, success, info);
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
