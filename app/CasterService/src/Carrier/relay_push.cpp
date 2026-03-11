#include "relay_push.h"
#include "knt.h"
#include "base64.h"

#define __class__ "relay_push"

relay_push::relay_push(ConnectInfo info) : carrier_base(info)
{
}

relay_push::~relay_push()
{
}

int relay_push::init()
{
    return 0;
}

int relay_push::start()
{
    // 创建一个bev连接并更新connect_key
    _connect_key = create_bev(_info.addr(), _info.port());

    // 启动Bev事件监听
    start_bev(true, 0, false, 0);
}

int relay_push::runing()
{

    return 0;
}

int relay_push::retry()
{
    // 这个函数就是bev的回调触发的  因此不能在这个地方直接清理bev连接和重新建立连接  只能设置一个标记  在定时器回调函数中执行重连操作
    _reconnect = true;

    unsubscribe();

    return 0;
}

int relay_push::stop()
{
    // 停止Bev事件监听
    stop_bev();

    // caster注销
    caster_withdraw(CasterRegisterType::PULL_MPT);

    unsubscribe();

    // 将销毁操作放入消息队列，执行删除此对象
    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    return 0;
}

int relay_push::read_cb(bufferevent *bev)
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

int relay_push::write_cb(bufferevent *bev)
{
    return 0;
}

int relay_push::event_cb(bufferevent *bev, short events)
{
    if (events == BEV_EVENT_CONNECTED)
    {
        auto str = build_ntrip_request(ConnectType::CONNECT_TYPE_PUSH,
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

int relay_push::timeout_cb()
{
    if (_reconnect)
    {
        _reconnect = false;

        // 清理上下文
        destory_bev(_connect_key);

        start();
    }
}

int relay_push::login_cb(auth_reply *reply)
{
    return 0;
}

int relay_push::register_cb(caster_reply *reply)
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

// int relay_push::request_new_relay_server()
// {

//     bufferevent_setcb(_bev, ReadCallback, NULL, EventCallback, this);

//     // 验证完成，注册数据流到CasterCore
//     CASTER::Register_Rover_Record(_login_mpt.c_str(), _user_name.c_str(), _connect_key.c_str(), Caster_Register_Callback, this, CasterRegisterType::PUSH_USR);
//     CASTER::Set_Push_Rover_Info(_login_mpt.c_str(), _target_mpt.c_str(), "", 0);

//     return 0;
// }
