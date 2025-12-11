#include <sstream>
#include "CasterMonitor.h"
#include "AccountOperate.h"
#include "AliasOperate.h"
#include "excute/connectOperate.h"
#include "excute/updateOperate.h"
#include "pullOperate.h"
#include "pushOperate.h"


CasterMonitor::CasterMonitor(QObject *parent) : QObject(parent)
{
    _logger = spdlog::default_logger();

    _caster_mgr->start();
    _auth_mgr->start();
}


CasterMonitor *CasterMonitor::create(QQmlEngine *, QJSEngine *) {
    return getInstance();
}

QVariantMap CasterMonitor::getNtripServerInfo(QString UID)
{
    auto iter= m_ntrip_server_map.find(UID);
    if(iter==m_ntrip_server_map.end())
    {
        ntrip_server obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(iter->second->info());
}

QVariantMap CasterMonitor::getNtripServerInfoByMpt(QString Mpt)
{
    auto map_item= _ntrip_serverUID_map.find(Mpt);
    if(map_item==_ntrip_serverUID_map.end())
    {
        ntrip_server obj;
        return JsonToQVariantMap(obj.info());
    }

    return getNtripServerInfo(map_item->second);
}

QVariantMap CasterMonitor::getNtripClientInfo(QString UID)
{
    auto iter= m_ntrip_client_map.find(UID);
    if(iter==m_ntrip_client_map.end())
    {
        ntrip_client obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(iter->second->info());
}

QVariantMap CasterMonitor::getUserAccountInfo(QString UID)
{
    auto iter= m_user_account_map.find(UID);
    if(iter==m_user_account_map.end())
    {
        user_account obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(iter->second->info());
}

QVariantMap CasterMonitor::getRelayPullInfo(QString UID)
{
    auto iter= m_relay_pull_list_map.find(UID);
    if(iter==m_relay_pull_list_map.end())
    {
        user_account obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(iter->second->info());
}

QVariantMap CasterMonitor::getRelayPushInfo(QString UID)
{
    auto iter= m_relay_push_list_map.find(UID);
    if(iter==m_relay_push_list_map.end())
    {
        user_account obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(iter->second->info());
}

QVariantMap CasterMonitor::getAliasRuleInfo(QString UID)
{
    auto iter= m_alias_rule_map.find(UID);
    if(iter==m_alias_rule_map.end())
    {
        user_account obj;
        return JsonToQVariantMap(obj.info());
    }
    return JsonToQVariantMap(iter->second->info());
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
    connect(op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateCasterRedisCtx,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectCasterSuccess,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectCasterFailed,Qt::UniqueConnection); //,Qt::QueuedConnection);


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

QVariantMap CasterMonitor::genAccountTemp()
{
    user_account item;
    auto json_info= item.info();
    json_info.erase("update_flag");
    return JsonToQVariantMap(json_info);
}

QString CasterMonitor::addAddAccountOperate(QVariantMap account_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventAddAccount>();

    // 设置对象属性
    op->id(UID);
    op->account_info(account_info);

    // 连接信号和槽
    connect(op.get(),&EventAddAccount::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addSetAccountOperate(QVariantMap account_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventSetAccount>();

    // 设置对象属性
    op->id(UID);
    op->account_info(account_info);

    // 连接信号和槽
    connect(op.get(),&EventSetAccount::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addDelAccountOperate(QVariantMap account_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventDelAccount>();

    // 设置对象属性
    op->id(UID);
    op->account_info(account_info);

    // 连接信号和槽
    connect(op.get(),&EventDelAccount::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addGetAccountOperate(QVariantMap account_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventGetAccount>();

    // 设置对象属性
    op->id(UID);
    op->account_info(account_info);

    // 连接信号和槽
    connect(op.get(),&EventGetAccount::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QVariantMap CasterMonitor::genPullStreamTemp()
{
    relay_pull_item item;
    auto json_info= item.info();
    // json_info.erase("update_flag");
    return JsonToQVariantMap(json_info);
}

QString CasterMonitor::addAddPullStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventAddPull>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);

    // 连接信号和槽
    connect(op.get(),&EventAddPull::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addSetPullStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventSetPull>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);

    // 连接信号和槽
    connect(op.get(),&EventSetPull::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addDelPullStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventDelPull>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);
    // 连接信号和槽
    connect(op.get(),&EventDelPull::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addGetPullStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventGetPull>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);
    // 连接信号和槽
    connect(op.get(),&EventGetPull::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QVariantMap CasterMonitor::genPushStreamTemp()
{
    relay_push_item item;
    auto json_info= item.info();
    // json_info.erase("update_flag");
    return JsonToQVariantMap(json_info);
}

QString CasterMonitor::addAddPushStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventAddPush>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);
    // 连接信号和槽
    connect(op.get(),&EventAddPush::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addSetPushStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventSetPush>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);
    // 连接信号和槽
    connect(op.get(),&EventSetPush::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addDelPushStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventDelPush>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);
    // 连接信号和槽
    connect(op.get(),&EventDelPush::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addGetPushStreamOperate(QVariantMap relay_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventGetPush>();

    // 设置对象属性
    op->id(UID);
    op->relay_info(relay_info);
    // 连接信号和槽
    connect(op.get(),&EventGetPush::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QVariantMap CasterMonitor::genAliasRuleTemp()
{
    user_account item;
    auto json_info= item.info();
    // json_info.erase("update_flag");
    return JsonToQVariantMap(json_info);
}

QString CasterMonitor::addAddAliasRuleOperate(QVariantMap alias_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventAddAlias>();

    // 设置对象属性
    op->id(UID);
    op->alias_info(alias_info);
    // 连接信号和槽
    connect(op.get(),&EventAddAlias::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addSetAliasRuleOperate(QVariantMap alias_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventSetAlias>();

    // 设置对象属性
    op->id(UID);
    op->alias_info(alias_info);
    // 连接信号和槽
    connect(op.get(),&EventSetAlias::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addDelAliasRuleOperate(QVariantMap alias_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventDelAlias>();

    // 设置对象属性
    op->id(UID);
    op->alias_info(alias_info);
    // 连接信号和槽
    connect(op.get(),&EventDelAlias::operateFinished,this,&CasterMonitor::onOperateFinished);

    // 添加到MAP中，等待任务执行
    _caster_redis_map.insert(std::pair(UID,op));
    return UID;
}

QString CasterMonitor::addGetAliasRuleOperate(QVariantMap alias_info)
{
    auto UID = generate_UniqueKey();
    // 创建对象
    auto op = std::make_shared<EventGetAlias>();

    // 设置对象属性
    op->id(UID);
    op->alias_info(alias_info);
    // 连接信号和槽
    connect(op.get(),&EventDelAlias::operateFinished,this,&CasterMonitor::onOperateFinished);

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

QString CasterMonitor::cancelOperate(QString op_uid)
{
    if(_caster_event_map.find(op_uid)!=_caster_event_map.end())
    {

    }
    if(_caster_redis_map.find(op_uid)!=_caster_redis_map.end())
    {

    }
    if(_auth_event_map.find(op_uid)!=_auth_event_map.end())
    {

    }
    if(_auth_redis_map.find(op_uid)!=_auth_redis_map.end())
    {

    }

    return QString();
}

QString CasterMonitor::deleteOperate(QString op_uid)
{
    if(_caster_event_map.find(op_uid)!=_caster_event_map.end())
    {

    }
    if(_caster_redis_map.find(op_uid)!=_caster_redis_map.end())
    {

    }
    if(_auth_event_map.find(op_uid)!=_auth_event_map.end())
    {

    }
    if(_auth_redis_map.find(op_uid)!=_auth_redis_map.end())
    {

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
    // 所有数据更新标识标志为false
    for(auto iter:m_caster_node_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_caster_node_map.find(key);
        if(item == m_caster_node_map.end())
        {
            auto obj= std::make_shared<caster_node>();
            m_caster_node_map.insert(std::pair(key,obj));
            item =  m_caster_node_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    //删除所有本次没有更新的元素
    auto it = m_caster_node_map.begin();
    while (it != m_caster_node_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_caster_node_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdataServerMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_ntrip_server_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_ntrip_server_map.find(key);
        if(item == m_ntrip_server_map.end())
        {
            auto obj= std::make_shared<ntrip_server>();
            m_ntrip_server_map.insert(std::pair(key,obj));
            item =  m_ntrip_server_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识

        // 更新挂载点-Connect_key映射表
        QString  alias_mpt=item->second->alias_mpt().c_str();
        auto map_item =  _ntrip_serverUID_map.find(alias_mpt);
        if(map_item == _ntrip_serverUID_map.end())
        {
            _ntrip_serverUID_map.insert(std::pair(alias_mpt,key));
        }
        else
        {
            map_item->second=key;
        }

    }

    //删除所有本次没有更新的元素
    auto it = m_ntrip_server_map.begin();
    while (it != m_ntrip_server_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_ntrip_server_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdataClientMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_ntrip_client_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_ntrip_client_map.find(key);
        if(item == m_ntrip_client_map.end())
        {
            auto obj= std::make_shared<ntrip_client>();
            m_ntrip_client_map.insert(std::pair(key,obj));
            item =  m_ntrip_client_map.find(key);
        }
        item->second->setInfo(info);


        auto server_item= get_ntrip_server_by_mpt(QString(item->second->alias_mpt().c_str()));

        if(server_item->position_update_time()!=0 && item->second->position_update_time()!=0)
        {
            item->second->distance(util_dist3d(server_item->ecef_x(),server_item->ecef_y(),server_item->ecef_z(),
                                               item->second->ecef_x(),item->second->ecef_y(),item->second->ecef_z()));
        }

        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_ntrip_client_map.begin();
    while (it != m_ntrip_client_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_ntrip_client_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }


    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdateAccountMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_user_account_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_user_account_map.find(key);
        if(item == m_user_account_map.end())
        {
            auto obj= std::make_shared<user_account>();
            m_user_account_map.insert(std::pair(key,obj));
            item =  m_user_account_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_user_account_map.begin();
    while (it != m_user_account_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_user_account_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdatePullListMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_relay_pull_list_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_relay_pull_list_map.find(key);
        if(item == m_relay_pull_list_map.end())
        {
            auto obj= std::make_shared<relay_pull_item>();
            m_relay_pull_list_map.insert(std::pair(key,obj));
            item =  m_relay_pull_list_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_relay_pull_list_map.begin();
    while (it != m_relay_pull_list_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_relay_pull_list_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    // emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdatePullStatMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_relay_pull_stat_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_relay_pull_stat_map.find(key);
        if(item == m_relay_pull_stat_map.end())
        {
            auto obj= std::make_shared<relay_pull_stat>();
            m_relay_pull_stat_map.insert(std::pair(key,obj));
            item =  m_relay_pull_stat_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_relay_pull_stat_map.begin();
    while (it != m_relay_pull_stat_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_relay_pull_stat_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    emit operateFinished(OP_UID,success,info);
}


void CasterMonitor::onUpdatePushListMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_relay_push_list_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_relay_push_list_map.find(key);
        if(item == m_relay_push_list_map.end())
        {
            auto obj= std::make_shared<relay_push_item>();
            m_relay_push_list_map.insert(std::pair(key,obj));
            item =  m_relay_push_list_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_relay_push_list_map.begin();
    while (it != m_relay_push_list_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_relay_push_list_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    // emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdatePushStatMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_relay_push_stat_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_relay_push_stat_map.find(key);
        if(item == m_relay_push_stat_map.end())
        {
            auto obj= std::make_shared<relay_push_stat>();
            m_relay_push_stat_map.insert(std::pair(key,obj));
            item =  m_relay_push_stat_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_relay_push_stat_map.begin();
    while (it != m_relay_push_stat_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_relay_push_stat_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

    emit operateFinished(OP_UID,success,info);
}

void CasterMonitor::onUpdateAliasMap(QString OP_UID, bool success, QVariantMap info)
{
    // 所有数据更新标识标志为false
    for(auto iter:m_alias_rule_map)
    {
        iter.second->update_flag(false);
    }
    //将数据更新到本地的context中去

    // 遍历所有 key-value
    for (auto it = info.begin(); it != info.end(); ++it) {
        QString key = it.key();
        QString value = it.value().toString();
        auto info = QStringToJson(value);

        auto item =  m_alias_rule_map.find(key);
        if(item == m_alias_rule_map.end())
        {
            auto obj= std::make_shared<alias_rule>();
            m_alias_rule_map.insert(std::pair(key,obj));
            item =  m_alias_rule_map.find(key);
        }
        item->second->setInfo(info);
        item->second->update_flag(true); //设置数据更新标识
    }

    auto it = m_alias_rule_map.begin();
    while (it != m_alias_rule_map.end()) {
        if (it->second->update_flag() == false) {
            it = m_alias_rule_map.erase(it);  // 删除元素，并更新迭代器
        } else {
            ++it;  // 仅在未删除时前进迭代器
        }
    }

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

    auto item= m_ntrip_server_map.find(map_item->second);
    if(item==m_ntrip_server_map.end())
    {
        return std::make_shared<ntrip_server>();
    }

    return item->second;
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
