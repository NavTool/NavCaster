#include "CasterMonitor.h"
#include "excute/connectRedis.h"
#include "excute/disconnectRedis.h"


CasterMonitor::CasterMonitor(QObject *parent) : QObject(parent)
{
    _logger = spdlog::default_logger();

    _caster_mgr->start();
    _auth_mgr->start();
}


CasterMonitor *CasterMonitor::create(QQmlEngine *, QJSEngine *) {
    return getInstance();
}

bool CasterMonitor::init_Caster_Connect(QVariantMap connect_info)
{
    if(_caster_con->_is_connected)
    {
        noticeError("Caster Network is already in a connected state!");
        return false;
    }

    // 设置参数
    _caster_con->ip(connect_info["ip"].toString());
    _caster_con->port(connect_info["port"].toInt());
    _caster_con->auth(connect_info["auth"].toString());

    // 添加到任务队列
    auto id= _caster_mgr->postTask(_caster_con);

    if(id==0)
    {
        return false;
    }

    // // 添加事件保存到上下文
    // _event_map.insert(std::pair(id,op));
    // 连接op的信号到CasterMonitor的槽函数
    connect(_caster_con.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateCasterRedisCtx,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(_caster_con.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectCasterSuccess,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(_caster_con.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectCasterFailed,Qt::UniqueConnection); //,Qt::QueuedConnection);

    return true;
}

bool CasterMonitor::init_Auth_Connect(QVariantMap connect_info)
{
    if(_auth_con->_is_connected)
    {
        noticeError("Auth Network is already in a connected state!");
        return false;
    }

    // 设置参数
    _auth_con->ip(connect_info["ip"].toString());
    _auth_con->port(connect_info["port"].toInt());
    _auth_con->auth(connect_info["auth"].toString());

    // 添加到任务队列
    auto id= _auth_mgr->postTask(_auth_con);

    if(id == 0)
    {
        return false;
    }

    // 添加事件保存到上下文
    // _event_map.insert(std::pair(id,op));
    // 连接op的信号到CasterMonitor的槽函数
    connect(_auth_con.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateAuthRedisCtx,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(_auth_con.get(),&EventConnectRedis::connectRedisSuccess,this,&CasterMonitor::onConnectAuthSuccess,Qt::UniqueConnection); //,Qt::QueuedConnection);
    connect(_auth_con.get(),&EventConnectRedis::connectRedisFailed,this,&CasterMonitor::onConnectAuthFailed,Qt::UniqueConnection); //,Qt::QueuedConnection);

    return true;
}

bool CasterMonitor::close_Caster_Connect()
{
    auto op = std::make_shared<EventDisconnectRedis>();

    op->set_redis_ctx(_caster_mgr->redisCtx());

    auto id= _caster_mgr->postTask(op);
    if(id == 0)
    {
        return false;
    }

    _event_map.insert(std::pair(id,op));
    return true;
}

bool CasterMonitor::close_Auth_Connect()
{
    auto op = std::make_shared<EventDisconnectRedis>();

    op->set_redis_ctx(_auth_mgr->redisCtx());

    auto id= _auth_mgr->postTask(op);
    if(id == 0)
    {
        return false;
    }

    _event_map.insert(std::pair(id,op));
    return true;
}

bool CasterMonitor::excute_caster_event(std::shared_ptr<EventOperationBase> op)
{
    auto id= _caster_mgr->postTask(op);
    if(id == 0)
    {
        return false;
    }
    _event_map.insert(std::pair(id,op));
    return true;
}

bool CasterMonitor::excute_caster_redis(std::shared_ptr<RedisOperationBase> op)
{
    auto id= _caster_mgr->postRedisTask(op);
    if(id == 0)
    {
        return false;
    }
    _redis_map.insert(std::pair(id,op));
    return true;
}

bool CasterMonitor::excute_auth_event(std::shared_ptr<EventOperationBase> op)
{
    auto id= _auth_mgr->postTask(op);
    if(id == 0)
    {
        return false;
    }
    _event_map.insert(std::pair(id,op));
    return true;
}

bool CasterMonitor::excute_auth_redis(std::shared_ptr<RedisOperationBase> op)
{
    auto id= _auth_mgr->postRedisTask(op);
    if(id == 0)
    {
        return false;
    }

    _redis_map.insert(std::pair(id,op));
    return true;
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
