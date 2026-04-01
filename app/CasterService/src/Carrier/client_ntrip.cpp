#include "client_ntrip.h"

client_ntrip::client_ntrip(ConnectInfo info) : carrier_base(info)
{
    __class__ = "client_ntrip";
}

client_ntrip::~client_ntrip()
{
}

int client_ntrip::init()
{
    return 0;
}

// ============ 流程：auth_login → login_cb → caster_register → register_cb → subscribe → subscribe_cb(OK) → running ============

int client_ntrip::start()
{
    auth_login(AuthType::CLIENT);
    return 0;
}

int client_ntrip::stop()
{
    stop_bev();
    auth_logout();
    unsubscribe();
    caster_withdraw();

    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
    return 0;
}

int client_ntrip::read_cb(bufferevent *bev)
{
    // 数据来自 subscribe 回调推送，read_cb 无需处理
    return 0;
}

int client_ntrip::write_cb(bufferevent *bev)
{
    return 0;
}

int client_ntrip::event_cb(bufferevent *bev, short events)
{
    spdlog::info("[{}:{}]: event stop, mount [{}], addr:[{}:{}]", __class__, __func__, _info.mount_point(), _info.addr(), _info.port());
    stop();
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
        caster_register(CasterRegisterType::CLIENT);
        break;
    case AuthReply::ERR:
        spdlog::warn("[{}:{}]: auth login failed, addr:[{}:{}]", __class__, __func__, _info.addr(), _info.port());
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
        spdlog::warn("[{}:{}]: caster register failed, addr:[{}:{}]", __class__, __func__, _info.addr(), _info.port());
        stop();
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
    {
        // 订阅成功，进入 running 状态
        start_bev(true, 0, false, 0);
        start_timeout_event(5);
        auto str = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);
        send_data(str.c_str(), str.size(), false);
        spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
        break;
    }
    case CasterReply::ERR:
        spdlog::warn("[{}:{}]: subscribe failed, addr:[{}:{}]", __class__, __func__, _info.addr(), _info.port());
        stop();
        break;
    default:
        break;
    }
    return 0;
}
