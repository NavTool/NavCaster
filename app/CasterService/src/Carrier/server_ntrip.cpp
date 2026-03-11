#include "server_ntrip.h"
#include "knt.h"

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

int server_ntrip::start()
{
    auth_login(AuthType::SERVER);
    return 0;
}

int server_ntrip::runing()
{
    // 启动Bev事件监听
    start_bev(true, 0, false, 0);

    start_timeout_event(5);

    // 构造回复消息
    auto str = build_nrtip_reply(CONNECT_TYPE_SERVER, _ntrip_version2, _transfer_with_chunked);

    // 发送回复消息
    send_data(str.c_str(), str.size(), false);

    spdlog::info("[{}]: mount [{}] is online, addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());

    return 0;
}

int server_ntrip::stop()
{
    // 停止Bev事件监听
    stop_bev();

    // 用户下线
    auth_logout();

    // caster注销
    caster_withdraw();

    // 将销毁操作放入消息队列，执行删除此对象
    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: mount [{}] is offline, addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());

    return 0;
}

int server_ntrip::read_cb(bufferevent *bev)
{
    // 读取数据 如果是chunked的数据，会将数据合并成完整的一块数据
    auto data = read_data(_transfer_with_chunked);

    // 解析数据

    // 发布数据
    std::string str_data(data.begin(), data.end());
    publish_data(str_data.c_str(), str_data.size());

    return 0;
}

int server_ntrip::event_cb(bufferevent *bev, short events)
{
    spdlog::info("[{}:{}]: stop mount [{}], addr:[{}:{}]", __class__, __func__, _info.mount_point(), _info.addr(), _info.port());
    stop();
    return 0;
}

int server_ntrip::timeout_cb()
{
    // 发送心跳包

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
        // spdlog::info("[{}]: AUTH_REPLY_ERROR user [{}] , using mount [{}], addr:[{}:{}]", __class__, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
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
        runing();
        break;
    case CasterReply::ERR:
        // spdlog::info("[{}:{}]: CASTER_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        stop();
        break;
    case CasterReply::ACTIVE:
        break;
    case CasterReply::INACTIVE:
        break;
    default:
        break;
    }
    return 0;
}
