#include "client_alias.h"
#include "knt.h"
#include <iostream>

#define __class__ "client_alias"

client_alias::client_alias(json req, bufferevent *bev)
{
    _conf = req["Settings"];
    _info = req;
    _bev = bev;

    _user_name = _info["user_name"];
    _mount_point = _info["mount_point"];
    _connect_key = _info["connect_key"];
    if (_info["ntrip_version"] == "Ntrip/2.0")
    {
        _NtripVersion2 = true;
    }
    if (_info["http_chunked"] == "chunked")
    {
        _transfer_with_chunked = true;
    }

    int fd = bufferevent_getfd(_bev);
    _ip = util_get_user_ip(fd);
    _port = util_get_user_port(fd);

    _send_evbuf = evbuffer_new();
    _recv_evbuf = evbuffer_new();

    _connect_timeout = _conf["Connect_Timeout"];
    _unsend_byte_limit = _conf["Unsend_Byte_Limit"];
}

client_alias::~client_alias()
{
    // auto fd = bufferevent_getfd(_bev);
    // evutil_closesocket(fd);
    bufferevent_free(_bev);
    evbuffer_free(_send_evbuf);
    evbuffer_free(_recv_evbuf);

    spdlog::info("[{}]:delete user [{}], using mount [{}], addr:[{}:{}]", __class__, _user_name, _mount_point, _ip, _port);
}

int client_alias::start()
{
    bufferevent_setcb(_bev, ReadCallback, NULL, EventCallback, this);

    AUTH::Add_Login_Record(_user_name.c_str(), _connect_key.c_str(), Auth_Login_Callback, this, AuthType::CLIENT);

    return 0;
}

int client_alias::runing()
{
    bufferevent_enable(_bev, EV_READ);

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
        _timeout_ev = event_new(bufferevent_get_base(_bev), -1, EV_PERSIST, TimeoutCallback, this);
        event_add(_timeout_ev, &_timeout_tv);
        _timeout_ev_flag = true;
    }

    // 添加一个请求，订阅指定频道数据
    CASTER::Sub_Alias_Raw_Data(_mount_point.c_str(), _user_name.c_str(), _connect_key.c_str(), Caster_Sub_Callback, this);

    return 0;
}

int client_alias::stop()
{

    if (_timeout_ev_flag == true)
    {
        event_del(_timeout_ev);
        event_free(_timeout_ev);
        _timeout_ev_flag = false;
    }

    bufferevent_disable(_bev, EV_READ);

    json close_req;
    close_req["origin_req"] = _info;
    close_req["req_type"] = CLOSE_NTRIP_CLIENT;
    QUEUE::Push(close_req);

    CASTER::Withdraw_Rover_Record(_mount_point.c_str(), _user_name.c_str(), _connect_key.c_str());
    CASTER::Unsub_Base_Raw_Data(_mount_point.c_str(), _connect_key.c_str());

    AUTH::Add_Logout_Record(_user_name.c_str(), _connect_key.c_str(), AuthType::CLIENT);

    spdlog::info("[{}]: user [{}] is logout, using mount [{}], addr:[{}:{}]", __class__, _user_name, _mount_point, _ip, _port);

    return 0;
}
int client_alias::bev_send_reply()
{
    if (_NtripVersion2)
    {
        evbuffer_add_printf(_send_evbuf, "HTTP/1.1 200 OK\r\n");
        evbuffer_add_printf(_send_evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
        evbuffer_add_printf(_send_evbuf, "Server: Ntrip ExampleCaster/2.0\r\n");
        evbuffer_add_printf(_send_evbuf, "Date: %s\r\n", util_get_http_date().c_str());
        evbuffer_add_printf(_send_evbuf, "Cache-Control: no-store, no-cache, max-age=0\r\n");
        evbuffer_add_printf(_send_evbuf, "Pragma: no-cache\r\n");
        evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
        evbuffer_add_printf(_send_evbuf, "Content-Type: gnss/data\r\n");
        if (_transfer_with_chunked)
        {
            evbuffer_add_printf(_send_evbuf, "Transfer-Encoding: chunked\r\n");
        }
        evbuffer_add_printf(_send_evbuf, "\r\n");
    }
    else
    {
        evbuffer_add_printf(_send_evbuf, "ICY 200 OK\r\n");
        evbuffer_add_printf(_send_evbuf, "\r\n");
    }

    bufferevent_write_buffer(_bev, _send_evbuf);
    return 0;
}

void client_alias::ReadCallback(bufferevent *bev, void *arg)
{
    auto svr = static_cast<client_alias *>(arg);
    bufferevent_read_buffer(bev, svr->_recv_evbuf);
    svr->publish_recv_raw_data();
}

void client_alias::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto svr = static_cast<client_alias *>(arg);

    spdlog::info("[{}:{}]: {}{}{}{}{}{} , user [{}], mount [{}], addr:[{}:{}]",
                 __class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-", svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);

    svr->stop();
}

void client_alias::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *svr = static_cast<client_alias *>(arg);
    // 定时函数已经被停止，该次调用不处理
    if (svr->_timeout_ev_flag == false)
    {
        return;
    }
    // svr->send_heart_beat_to_server();
    svr->update_tcp_delay_info();
}

int client_alias::transfer_sub_raw_data(const char *data, size_t length)
{
    auto UnsendBufferSize = evbuffer_get_length(bufferevent_get_output(_bev));

    if (_unsend_byte_limit > 0 && UnsendBufferSize > _unsend_byte_limit)
    {
        spdlog::info("[{}:{}: send to user [{}]'s date unsend size is too large :[{}], close the connect! using mount [{}], addr:[{}:{}]", __class__, __func__, _user_name, UnsendBufferSize, _mount_point, _ip, _port);
        stop();
        return -1;
    }

    if (_transfer_with_chunked)
    {
        evbuffer_add_printf(_send_evbuf, "%lx\r\n", length);
        evbuffer_add(_send_evbuf, data, length);
        evbuffer_add(_send_evbuf, "\r\n", 2);
        bufferevent_write_buffer(_bev, _send_evbuf);
    }
    else
    {
        evbuffer_add(_send_evbuf, data, length);
        bufferevent_write_buffer(_bev, _send_evbuf);
    }
    return 0;
}

int client_alias::publish_recv_raw_data()
{
    size_t length = evbuffer_get_length(_recv_evbuf);
    char *data = new char[length + 1];
    data[length] = '\0';
    evbuffer_remove(_recv_evbuf, data, length);

    CASTER::Pub_Rover_Raw_Data(_user_name.c_str(), _connect_key.c_str(), data, length);
    _str_decoder.Decode(data, length);
    if (_str_decoder._has_position)
    {
        CASTER::Set_Rover_Coord_Info(_user_name.c_str(), _connect_key.c_str(),
                                     _str_decoder._ecef_x, _str_decoder._ecef_y, _str_decoder._ecef_z,
                                     _str_decoder._position_update_time,
                                     _str_decoder._quality,
                                     _str_decoder._sat_num, _str_decoder._diff);
    }

    delete[] data;
    return 0;
}

int client_alias::update_tcp_delay_info()
{
    return CASTER::Set_Rover_Delay_Info(_user_name.c_str(), _connect_key.c_str(), util_get_tcp_delay(bufferevent_getfd(_bev)));
}

void client_alias::Auth_Login_Callback(const char *request, void *arg, auth_reply *reply)
{
    auto svr = static_cast<client_alias *>(arg);

    switch (reply->type)
    {
    case AuthReply::OK:
        CASTER::Register_Rover_Record(svr->_mount_point.c_str(), svr->_user_name.c_str(), svr->_connect_key.c_str(), Caster_Register_Callback, svr,CasterRegisterType::ALIAS_MPT);
        break;
    case AuthReply::ERR:
        spdlog::info("[{}:{}]: AUTH_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->stop();
        break;
    default:
        break;
    }
    // if (reply->type == AUTH_REPLY_OK)
    // {
    //     CASTER::Register_Rover_Record(svr->_user_name.c_str(), svr->_connect_key.c_str(), Caster_Register_Callback, svr);
    // }
    // else
    // {
    //     spdlog::info("[{}:{}]: AUTH_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
    //     svr->stop();
    // }
}

void client_alias::Caster_Register_Callback(const char *request, void *arg, catser_reply *reply)
{
    auto svr = static_cast<client_alias *>(arg);
    switch (reply->type)
    {
    case CasterReply::OK:
        svr->runing();
        break;
    case CasterReply::ERR:
        spdlog::info("[{}:{}]: CASTER_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->stop();
        break;
    case CasterReply::ACTIVE:
        // spdlog::info("[{}:{}]: CASTER_REPLY_ACTIVE:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        break;
    case CasterReply::INACTIVE:
        // spdlog::info("[{}:{}]: CASTER_REPLY_INACTIVE:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        break;
    default:
        break;
    }
}

void client_alias::Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply)
{
    auto svr = static_cast<client_alias *>(arg);

    if (reply->type == CasterReply::STRING)
    {
        svr->transfer_sub_raw_data(reply->str, reply->len);
    }
    else if (reply->type == CasterReply::OK)
    {
        spdlog::info("[{}]: user [{}] is login, using mount [{}], addr:[{}:{}]", __class__, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);

        svr->bev_send_reply();
    }
    else if (reply->type == CasterReply::ERR)
    {
        spdlog::info("[{}:{}]: CASTER_REPLY_ERR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->stop();
    }
}
