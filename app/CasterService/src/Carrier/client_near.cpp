#include "client_near.h"

client_near::client_near(ConnectInfo info) : carrier_base(info)
{
    __class__ = "client_near";
}

client_near::~client_near()
{
}

int client_near::init()
{
    return 0;
}

// ============ 流程：auth_login → login_cb → caster_register(NEAREST) → register_cb(OK) → running → read_cb解析NMEA触发subscribe ============

int client_near::start()
{
    auth_login(AuthType::CLIENT);
    return 0;
}

int client_near::stop()
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

int client_near::read_cb(bufferevent *bev)
{
    auto data = read_data(false);

    // 尝试解析数据流

    // 根据解析到的位置订阅基站

    return 0;
}

int client_near::write_cb(bufferevent *bev)
{
    return 0;
}

int client_near::event_cb(bufferevent *bev, short events)
{
    spdlog::info("[{}:{}]: event stop, mount [{}], addr:[{}:{}]", __class__, __func__, _info.mount_point(), _info.addr(), _info.port());
    stop();
    return 0;
}

int client_near::timeout_cb()
{
    return 0;
}

int client_near::login_cb(auth_reply *reply)
{
    switch (reply->type)
    {
    case AuthReply::OK:
        caster_register(CasterRegisterType::NEAREST);
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

int client_near::register_cb(caster_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::OK:
    {
        // 注册成功，进入 running 状态（不立即订阅，等NMEA数据触发）
        start_bev(true, 0, false, 0);
        start_timeout_event(5);
        auto str = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);
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

int client_near::subscribe_cb(caster_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::STRING:
        send_data(reply->str, reply->len, _transfer_with_chunked);
        break;
    case CasterReply::OK:
        break;
    case CasterReply::ERR:
        spdlog::warn("[{}:{}]: subscribe failed, addr:[{}:{}]", __class__, __func__, _info.addr(), _info.port());
        stop();
        break;
    default:
        break;
    }
    return 0;
}

// int client_near::try_sub_near_station()
// {
//     // 已完成坐标解析，检索最近基站
//     double distance = util_dist3d(_ecef_x, _ecef_y, _ecef_z,
//                                   _str_decoder._ecef_x, _str_decoder._ecef_y, _str_decoder._ecef_z);

//     if (distance > 1000.0) // 判断旧的坐标和新的坐标的距离差异是否超过1km，如果已经超过，那就触发订阅函数
//     {
//         _ecef_x = _str_decoder._ecef_x;
//         _ecef_y = _str_decoder._ecef_y;
//         _ecef_z = _str_decoder._ecef_z;

//         // 调用GEO命令查询最近基站
//         double lat = 0.0, lon = 0.0, alt = 0.0;
//         util_ecef2pos(_str_decoder._ecef_x, _str_decoder._ecef_y, _str_decoder._ecef_z, lat, lon, alt);

//         _lat = lat;
//         _lon = lon;
//         CASTER::Sub_Near_Raw_Data(_alias_mpt.c_str(), lat, lon, _user_name.c_str(), _connect_key.c_str(), Caster_Sub_Callback, this); // 这里要传递的是实际订阅的挂载点，因为重复订阅会先将前一个实际订阅的挂载点给取消掉，然后挂到新的挂载点上，同时调用回调告知新的挂载点
//     }
//     return 0;
// }
