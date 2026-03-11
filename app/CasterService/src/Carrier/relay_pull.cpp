#include "relay_pull.h"
#include "knt.h"
#include "base64.h"

#define __class__ "relay_pull"

relay_pull::relay_pull(ConnectInfo info) : carrier_base(info)
{
}

relay_pull::~relay_pull()
{
}

int relay_pull::init()
{
    return 0;
}

int relay_pull::start()
{
    // 创建一个bev连接并更新connect_key
    _connect_key = create_bev(_info.addr(), _info.port());

    // 启动Bev事件监听
    start_bev(true, 0, false, 0);

    return 0;
}

int relay_pull::runing()
{

    return 0;
}

int relay_pull::retry()
{
    // 这个函数就是bev的回调触发的  因此不能在这个地方直接清理bev连接和重新建立连接  只能设置一个标记  在定时器回调函数中执行重连操作
    _reconnect = true;
    return 0;
}

int relay_pull::stop()
{
    // 停止Bev事件监听
    stop_bev();

    // caster注销
    caster_withdraw(CasterRegisterType::PULL_MPT);

    // 将销毁操作放入消息队列，执行删除此对象
    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    return 0;
}

int relay_pull::read_cb(bufferevent *bev)
{
    if (_connected)
    {
        auto data = read_data(_transfer_with_chunked);
    }
    else
    {
        auto data = read_data(false);
        _connected = verify_ntrip_response(reinterpret_cast<const char *>(data.data()), data.size(), _ntrip_version2, _transfer_with_chunked) == 0;

        if (_connected)
        {
            caster_register(CasterRegisterType::PULL_MPT);
        }
    }
    return 0;
}

int relay_pull::write_cb(bufferevent *bev)
{
    return 0;
}

int relay_pull::event_cb(bufferevent *bev, short events)
{
    if (events == BEV_EVENT_CONNECTED)
    {
        auto str = build_ntrip_request(ConnectType::CONNECT_TYPE_PULL,
                                       _ntrip_version2,
                                       _info.mount_point(),
                                       _info.http_host(),
                                       _info.ntrip_auth());

        send_data(str.c_str(), str.size(), false);
    }
    else
    {
        retry();
    }
    return 0;
}

int relay_pull::timeout_cb()
{

    if (_reconnect)
    {
        _reconnect = false;

        // 清理上下文
        destory_bev(_connect_key);

        start();
    }

    return 0;
}

int relay_pull::login_cb(auth_reply *reply)
{
    return 0;
}

int relay_pull::register_cb(caster_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::OK:
        runing();
        break;
    case CasterReply::ERR:
        retry();
        break;
    case CasterReply::ACTIVE:
        break;
    case CasterReply::INACTIVE:
        break;
    default:
        break;
    }
}

// int relay_pull::send_heart_beat_to_server()
// {
//     return 0;
// }

// void relay_pull::ReconnectCallback(evutil_socket_t fd, short events, void *arg)
// {
//     auto svr = static_cast<relay_pull *>(arg);

//     svr->start();
// }
