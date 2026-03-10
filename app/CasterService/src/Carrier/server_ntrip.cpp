#include "server_ntrip.h"
#include "knt.h"
#define __class__ "server_ntrip"

server_ntrip::server_ntrip(ConnectInfo info) : carrier_base(info)
{
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
    start_bev(true, 0, false, 0);

    auth_login(AuthType::SERVER);

    return 0;
}

int server_ntrip::stop()
{

    stop_bev();

    auth_logout();

    caster_withdraw();

    CASTER::Withdraw_Base_Record(_login_mpt.c_str(), _user_name.c_str(), _connect_key.c_str());

    AUTH::Add_Logout_Record(_user_name.c_str(), _connect_key.c_str(), AuthType::SERVER);

    spdlog::info("[{}]: mount [{}] is offline, addr:[{}:{}]", __class__, _login_mpt, _ip, _port);

    return 0;
}

int server_ntrip::read_cb(bufferevent *bev)
{
    return 0;
}

int server_ntrip::write_cb(bufferevent *bev)
{
    return 0;
}

int server_ntrip::event_cb(bufferevent *bev, short events)
{
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
        caster_register();
        break;
    case AuthReply::ERR:
        spdlog::info("[{}]: AUTH_REPLY_ERROR user [{}] , using mount [{}], addr:[{}:{}]", __class__, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        stop();
        break;
    default:
        break;
    }

    return 0;
}

int server_ntrip::register_cb(catser_reply *reply)
{
    switch (reply->type)
    {
    case CasterReply::OK:
        runing();
        break;
    case CasterReply::ERR:
        spdlog::info("[{}:{}]: CASTER_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
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

int server_ntrip::runing()
{
    bufferevent_enable(_bev, EV_READ | EV_WRITE);

    if (_connect_timeout > 0)
    {
        _bev_read_timeout_tv.tv_sec = _connect_timeout;
        _bev_read_timeout_tv.tv_usec = 0;
        bufferevent_set_timeouts(_bev, &_bev_read_timeout_tv, NULL);
    }

    if (_timeout_ev_flag == false)
    {
        _timeout_tv.tv_sec = 1;
        _timeout_tv.tv_usec = 0;
        event_add(_timeout_ev, &_timeout_tv);
        _timeout_ev_flag = true;
    }

    bev_send_reply();

    spdlog::info("[{}]: mount [{}] is online, addr:[{}:{}]", __class__, _login_mpt, _ip, _port);

    return 0;
}

void server_ntrip::ReadCallback(bufferevent *bev, void *arg)
{
    auto svr = static_cast<server_ntrip *>(arg);
    bufferevent_read_buffer(bev, svr->_recv_evbuf);
    svr->publish_recv_raw_data();
}

// void server_ntrip::EventCallback(bufferevent *bev, short events, void *arg)
// {
//     auto svr = static_cast<server_ntrip *>(arg);

//     spdlog::info("[{}:{}]: {}{}{}{}{}{} , mount [{}], addr:[{}:{}]",
//                  __class__, __func__,
//                  (events & BEV_EVENT_READING) ? "read" : "-",
//                  (events & BEV_EVENT_WRITING) ? "write" : "-",
//                  (events & BEV_EVENT_EOF) ? "eof" : "-",
//                  (events & BEV_EVENT_ERROR) ? "error" : "-",
//                  (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
//                  (events & BEV_EVENT_CONNECTED) ? "connected" : "-", svr->_login_mpt, svr->_ip, svr->_port);

//     svr->stop();
// }

// void server_ntrip::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
// {
//     auto *svr = static_cast<server_ntrip *>(arg);

//     // 定时函数已经被停止，该次调用不处理
//     if (svr->_timeout_ev_flag == false)
//     {
//         return;
//     }

//     svr->send_heart_beat_to_server();
//     svr->update_tcp_delay_info();
// }

// int server_ntrip::send_heart_beat_to_server()
// {
//     // 检测是否要发送心跳包
//     if (_heart_beat_interval <= 0)
//     {
//         return 0;
//     }

//     // 判断距离上次发送心跳包时间间隔是否达到要求
//     time_t now_time = util_get_now_second();
//     if (now_time - _last_heart_beat_time < _heart_beat_interval)
//     {
//         return 0;
//     }
//     else
//     {
//         _last_heart_beat_time = now_time; // 更新发送时间

//         // 发送心跳包
//         auto UnsendBufferSize = evbuffer_get_length(bufferevent_get_output(_bev));
//         if (_unsend_byte_limit > 0 && UnsendBufferSize > _unsend_byte_limit)
//         {
//             spdlog::info("[{}:{}: send to server [{}]'s  unsend size is too large :[{}], close the connect! addr:[{}:{}]", __class__, __func__, _login_mpt, UnsendBufferSize, _ip, _port);
//             stop();
//         }
//         bufferevent_write(_bev, _heart_beat_msg.data(), _heart_beat_msg.size());
//         return 0;
//     }
// }
