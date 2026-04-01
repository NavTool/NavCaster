#include "relay_pull.h"

relay_pull::relay_pull(ConnectInfo info) : carrier_base(info)
{
    __class__ = "relay_pull";
}

relay_pull::~relay_pull()
{
}

int relay_pull::init()
{
    return 0;
}

// ============ 流程：create_bev连接 → event_cb(CONNECTED)发送请求 → read_cb验证握手 → caster_register → register_cb(OK) → running ============

int relay_pull::start()
{
    _connect_key = create_bev(_info.addr(), _info.port());
    start_bev(true, 0, false, 0);
    return 0;
}

int relay_pull::stop()
{
    stop_bev();
    caster_withdraw();

    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
    return 0;
}

int relay_pull::read_cb(bufferevent *bev)
{
    if (!_connected)
    {
        // relay握手阶段：验证服务端响应
        auto data = read_data(false);
        _connected = verify_ntrip_response(reinterpret_cast<const char *>(data.data()), data.size(), _ntrip_version2, _transfer_with_chunked) == 0;
        if (_connected)
        {
            caster_register(CasterRegisterType::PULL);
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
        // relay连接建立：发送NTRIP拉取请求
        auto str = build_ntrip_request(ConnectType::CONNECT_TYPE_PULL,
                                       _ntrip_version2,
                                       _info.mount_point(),
                                       _info.http_host(),
                                       _info.ntrip_auth());
        send_data(str.c_str(), str.size(), false);
    }
    else
    {
        _reconnect = true;
    }
    return 0;
}

int relay_pull::timeout_cb()
{
    if (_reconnect)
    {
        _reconnect = false;
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
    {
        // 注册成功，进入 running 状态
        start_bev(true, 0, false, 0);
        spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
        break;
    }
    case CasterReply::ERR:
        spdlog::warn("[{}:{}]: caster register failed, addr:[{}:{}]", __class__, __func__, _info.addr(), _info.port());
        _reconnect = true;
        break;
    default:
        break;
    }
    return 0;
}

int relay_pull::subscribe_cb(caster_reply *reply)
{
    return 0;
}
