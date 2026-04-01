#include "server_ntrip.h"

server_ntrip::server_ntrip(ConnectInfo info) : carrier_base(info)
{
    __class__ = "server_ntrip";
}

server_ntrip::~server_ntrip()
{
}

int server_ntrip::init()
{
    return 0;
}

// ============ 流程：auth_login → login_cb → caster_register → register_cb(OK) → running ============

int server_ntrip::start()
{
    auth_login(AuthType::SERVER);
    return 0;
}

int server_ntrip::stop()
{
    stop_bev();
    auth_logout();
    caster_withdraw();

    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
    return 0;
}

int server_ntrip::read_cb(bufferevent *bev)
{
    // 基站上传数据，读取后发布到 caster
    auto data = read_data(_transfer_with_chunked);
    publish_data(reinterpret_cast<const char *>(data.data()), data.size());
    return 0;
}

int server_ntrip::write_cb(bufferevent *bev)
{
    return 0;
}

int server_ntrip::event_cb(bufferevent *bev, short events)
{
    spdlog::info("[{}:{}]: event stop, mount [{}], addr:[{}:{}]", __class__, __func__, _info.mount_point(), _info.addr(), _info.port());
    stop();
    return 0;
}

int server_ntrip::timeout_cb()
{
    return 0;
}

int server_ntrip::login_cb(auth_reply *reply)
{
    switch (reply->type)
    {
    case AuthReply::OK:
        caster_register(CasterRegisterType::SERVER);
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

int server_ntrip::register_cb(caster_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::OK:
    {
        // 注册成功，进入 running 状态
        start_bev(true, 0, false, 0);
        start_timeout_event(5);
        auto str = build_nrtip_reply(CONNECT_TYPE_SERVER, _ntrip_version2, _transfer_with_chunked);
        send_data(str.c_str(), str.size(), false);
        spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
        break;
    }
    case CasterReply::ERR:
        spdlog::warn("[{}:{}]: caster register failed, addr:[{}:{}]", __class__, __func__, _info.addr(), _info.port());
        stop();
        break;
    default:
        break;
    }
    return 0;
}

int server_ntrip::subscribe_cb(caster_reply *reply)
{
    return 0;
}
