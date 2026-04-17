#include <sstream>
#include <random>
#include "CasterMonitor.h"
#include "knt.h"


CasterMonitor::CasterMonitor(QObject *parent) : QObject(parent)
{
    _logger = spdlog::default_logger();

    // ==================== 创建 HTTP 客户端 ====================
    _http_client = std::make_unique<HttpClient>(this);

    connect(_http_client.get(), &HttpClient::loginSuccess,  this, &CasterMonitor::onLoginSuccess);
    connect(_http_client.get(), &HttpClient::loginFailed,   this, &CasterMonitor::onLoginFailed);
    connect(_http_client.get(), &HttpClient::logoutFinished, this, &CasterMonitor::onLogoutFinished);

    // ==================== 绑定 HttpClient 到所有 HttpHashContext ====================

    // Auth 相关
    AccountRecords.setClient(_http_client.get());
    AccountRecords.setApiPath("/api/accounts");

    AccountActives.setClient(_http_client.get());
    AccountActives.setApiPath("/api/accounts/active");

    // Core 相关
    AccessGroups.setClient(_http_client.get());
    AccessGroups.setApiPath("/api/access/groups");

    AccessItems.setClient(_http_client.get());
    AccessItems.setApiPath("/api/access/items");

    SourceRecords.setClient(_http_client.get());
    SourceRecords.setApiPath("/api/sources");

    SourceStates.setClient(_http_client.get());
    SourceStates.setApiPath("/api/servers");

    ClientStates.setClient(_http_client.get());
    ClientStates.setApiPath("/api/clients");

    StreamStates.setClient(_http_client.get());
    StreamStates.setApiPath("/api/streams");

    AliasRules.setClient(_http_client.get());
    AliasRules.setApiPath("/api/aliases");

    PullRecords.setClient(_http_client.get());
    PullRecords.setApiPath("/api/relays/pull");

    PullStates.setClient(_http_client.get());
    PullStates.setApiPath("/api/relays/pull/status");

    PushRecords.setClient(_http_client.get());
    PushRecords.setApiPath("/api/relays/push");

    PushStates.setClient(_http_client.get());
    PushStates.setApiPath("/api/relays/push/status");

    // Service 相关
    CasterNodes.setClient(_http_client.get());
    CasterNodes.setApiPath("/api/nodes");

    // ==================== 注册 HttpHashContext 回调 ====================
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
    if(_connected)
    {
        noticeError("Already connected to API server!");
        return;
    }

    QString baseUrl = QString("http://%1:%2").arg(ip).arg(port);
    _http_client->setBaseUrl(baseUrl);

    // auth 格式：  "username:password"  或  "password"（默认用户 admin）
    QString username = "admin";
    QString password = auth;
    int colonIdx = auth.indexOf(':');
    if (colonIdx > 0)
    {
        username = auth.left(colonIdx);
        password = auth.mid(colonIdx + 1);
    }

    _http_client->login(username, password);
}

void CasterMonitor::disconnectCaster()
{
    if(!_connected)
    {
        noticeError("Not connected!");
        return;
    }
    _http_client->logout();
}

void CasterMonitor::connectAuth(const QString &, int, const QString &)
{
    // Auth 已通过 HTTP API 统一鉴权，直接发射成功信号保持向后兼容
    emit connectAuthSuccess();
}

void CasterMonitor::disconnectAuth()
{
    // 无需独立断开 auth 连接
    emit authDisconnected();
}

void CasterMonitor::onLoginSuccess()
{
    _connected = true;
    emit connectCasterSuccess();
    // 同时触发 auth 成功（向后兼容）
    emit connectAuthSuccess();
}

void CasterMonitor::onLoginFailed(const QString &error)
{
    _logger->error("HTTP API login failed: {}", error.toStdString());
    emit connectCasterFailed();
}

void CasterMonitor::onLogoutFinished()
{
    _connected = false;

    // 清理所有本地缓存
    AccountRecords.clear();
    AccountActives.clear();
    AccessGroups.clear();
    AccessItems.clear();
    SourceRecords.clear();
    SourceStates.clear();
    ClientStates.clear();
    StreamStates.clear();
    AliasRules.clear();
    PullRecords.clear();
    PullStates.clear();
    PushRecords.clear();
    PushStates.clear();
    CasterNodes.clear();
    _ntrip_serverUID_map.clear();

    emit casterDisconnected();
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
