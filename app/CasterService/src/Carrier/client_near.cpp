#include "client_near.h"
#include "knt.h"
#include <iostream>

#define __class__ "client_near"

client_near::client_near(ConnectInfo info) : carrier_base(info)
{
}

client_near::~client_near()
{
}

int client_near::init()
{
    return 0;
}

int client_near::start()
{
    auth_login(AuthType::CLIENT);
    return 0;
}

int client_near::runing()
{
    // 启动Bev事件监听
    start_bev(true, 0, false, 0);

    start_timeout_event(5);

    // 构造回复消息
    auto str = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);

    // 发送回复消息
    send_data(str.c_str(), str.size(), false);

    // spdlog::info("[{}]: user [{}] is login, using mount [{}], addr:[{}:{}]", __class__, _user_name, _login_mpt, _ip, _port);

    return 0;
}

int client_near::stop()
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
        caster_register(CasterRegisterType::NEAREST_MPT);
        break;
    case AuthReply::ERR:
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

int client_near::subscribe_cb(caster_reply *reply)
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
