#include "client_ntrip.h"
#include "knt.h"
#include <iostream>

#define __class__ "client_ntrip"

client_ntrip::client_ntrip(ConnectInfo info) : carrier_base(info)
{
}

client_ntrip::~client_ntrip()
{
}

int client_ntrip::init()
{
    return 0;
}

int client_ntrip::start()
{
    auth_login(AuthType::CLIENT);
    return 0;
}

int client_ntrip::runing()
{
    // 启动Bev事件监听
    start_bev(true, 0, false, 0);

    start_timeout_event(5);

    // 构造回复消息
    auto str = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);

    // 发送回复消息
    send_data(str.c_str(), str.size(), false);

    spdlog::info("[{}]: user [{}] is login, using mount [{}], addr:[{}:{}]", __class__, _info.user_name(), _info.mount_point(), _info.addr(), _info.port());

    return 0;
}

int client_ntrip::stop()
{
    // 停止Bev事件监听
    stop_bev();

    // 用户下线
    auth_logout(AuthType::CLIENT);

    // 取消订阅
    unsubscribe();

    // caster注销
    caster_withdraw(CasterRegisterType::NORMAL);

    // 将销毁操作放入消息队列，执行删除此对象
    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);
    // spdlog::info("[{}]: user [{}] is logout, using mount [{}], addr:[{}:{}]", __class__, _user_name, _login_mpt, _ip, _port);

    return 0;
}

int client_ntrip::read_cb(bufferevent *bev)
{
    return 0;
}

int client_ntrip::write_cb(bufferevent *bev)
{
    return 0;
}

int client_ntrip::event_cb(bufferevent *bev, short events)
{
    return 0;
}

int client_ntrip::timeout_cb()
{
    return 0;
}

int client_ntrip::login_cb(auth_reply *reply)
{
    switch (reply->type)
    {
    case AuthReply::OK:
        caster_register(CasterRegisterType::NORMAL);
        break;
    case AuthReply::ERR:
        // spdlog::info("[{}:{}]: AUTH_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        stop();
        break;
    default:
        break;
    }
    return 0;
}

int client_ntrip::register_cb(caster_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::OK:
        subscribe();
        break;
    case CasterReply::ERR:
        // spdlog::info("[{}:{}]: CASTER_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        stop();
        break;
    case CasterReply::ACTIVE:
        // spdlog::info("[{}:{}]: CASTER_REPLY_ACTIVE:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        break;
    case CasterReply::INACTIVE:
        // spdlog::info("[{}:{}]: CASTER_REPLY_INACTIVE:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        break;
    default:
        break;
    }
    return 0;
}

int client_ntrip::subscribe_cb(caster_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::STRING:
        send_data(reply->str, reply->len, _transfer_with_chunked);
        break;
    case CasterReply::OK:
        runing();
        break;
    case CasterReply::ERR:
        stop();
        break;
    default:
        break;
    }

    return 0;
}
