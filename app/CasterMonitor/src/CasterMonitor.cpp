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
    // 创建一个任务
    auto op= std::make_shared<EventConnectRedis>();

    // 设置参数
    op->ip(connect_info["ip"].toString());
    op->port(connect_info["port"].toInt());
    op->auth(connect_info["auth"].toString());

    // 添加到任务队列
    auto id= _caster_mgr->postTask(op);

    if(id==0)
    {
        return false;
    }

    // 添加事件保存到上下文
    _event_map.insert(std::pair(id,op));
    // 连接op的信号到CasterMonitor的槽函数    
    connect(op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateCasterRedisCtx); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisSuccess,this,[this](){emit connectCasterSuccess();}); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisFailed,this,[this](){emit connectCasterFailed();}); //,Qt::QueuedConnection);

    return true;
}

bool CasterMonitor::init_Auth_Connect(QVariantMap connect_info)
{
    // 创建一个任务
    auto op= std::make_shared<EventConnectRedis>();

    // 设置参数
    op->ip();
    op->port();
    op->auth();

    // 添加到任务队列
    auto id= _auth_mgr->postTask(op);

    if(id == 0)
    {
        return false;
    }

    // 添加事件保存到上下文
    _event_map.insert(std::pair(id,op));
    // 连接op的信号到CasterMonitor的槽函数
    connect(op.get(),&EventConnectRedis::updateRedisCtx,this,&CasterMonitor::onUpdateAuthRedisCtx); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisSuccess,this,[this](){emit connectAuthSuccess();}); //,Qt::QueuedConnection);
    connect(op.get(),&EventConnectRedis::connectRedisFailed,this,[this](){emit connectAuthFailed();}); //,Qt::QueuedConnection);

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

void CasterMonitor::onUpdateCasterRedisCtx(redisAsyncContext *ctx)
{
    // 更新
    _caster_mgr->setRedisCtx(ctx);
}

void CasterMonitor::onUpdateAuthRedisCtx(redisAsyncContext *ctx)
{
    _auth_mgr->setRedisCtx(ctx);
}
