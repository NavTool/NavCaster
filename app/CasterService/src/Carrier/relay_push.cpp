#include "relay_push.h"
#include "knt.h"
#include "base64.h"

#define __class__ "relay_push"

relay_push::relay_push(json info, event_base *base)
{
    _info = info;
    _login_mpt = info["login_mpt"];
    _type = info["type"];
    _target_ip = info["target_ip"];
    _target_port = info["target_port"];
    _target_mpt = info["target_mpt"];
    _target_account = info["target_account"];
    _target_password = info["target_password"];

    _connect_timeout = 0;

    _base = base;

    _send_evbuf = evbuffer_new();
    _recv_evbuf = evbuffer_new();

    _reconnect_ev = event_new(_base, -1, 0, ReconnectCallback, this);
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);
}

relay_push::~relay_push()
{

    evbuffer_free(_send_evbuf);
    evbuffer_free(_recv_evbuf);

    event_free(_reconnect_ev);
    event_free(_timeout_ev);

    spdlog::info("[{}]: delete mount [{}], addr:[{}:{}]", __class__, _login_mpt, _target_ip, _target_port);
}

int relay_push::start()
{
    // 创建连接
    std::string addr = _target_ip;
    std::string port = std::to_string(_target_port);

    _bev = bufferevent_socket_new(_base, -1, BEV_OPT_CLOSE_ON_FREE); //-1表示自动创建fd

    evutil_addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    evutil_getaddrinfo(addr.c_str(), port.c_str(), &hints, &res);

    // 创建一个绑定在base上的buffevent，并建立socket连接

    if (bufferevent_socket_connect(_bev, res->ai_addr, res->ai_addrlen))
    {
        // 连接建立失败
        bufferevent_free(_bev);

        return 1;
    }

    // 连接建立成功。返回port，连接建立失败，返回0
    auto fd = bufferevent_getfd(_bev);
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &len))
    {
        // 获取本地连接信息失败
        bufferevent_free(_bev);
        return 2;
    }

    _connect_timeout_tv.tv_sec = 300;
    _connect_timeout_tv.tv_usec = 0;
    bufferevent_setcb(_bev, VerifyCallback, NULL, ConnectedCallback, this);
    bufferevent_set_timeouts(_bev, &_connect_timeout_tv, NULL);
    bufferevent_enable(_bev, EV_READ);

    // 根据类型创建不同的数据流

    // 如果是TCP Client

    // 如果是Ntrip Client ，创建一个TCP连接，连接建立完成，发送验证信息回调

    // 如果是TCP Server（先不支持） 创建一个listener,维护所有连接,所有的连接的ReadEvent都统一发送到指定频道，没有就不播发

    return 0;
}

int relay_push::stop()
{
    if (_timeout_ev_flag == true)
    {
        event_del(_timeout_ev);
        _timeout_ev_flag = false;
    }

    if (_bev != nullptr)
    {
        bufferevent_free(_bev);
        _bev = nullptr;
    }

    // 向xx发送销毁请求
    json close_req;
    close_req["origin_req"] = _info;
    close_req["req_type"] = CLOSE_RELAY_PUSH;
    QUEUE::Push(close_req);

    CASTER::Withdraw_Rover_Record(_login_mpt.c_str(), _user_name.c_str(), _connect_key.c_str());

    spdlog::info("[{}]: Relay Push [{}] is stop, addr:[{}:{}]", __class__, _login_mpt, _target_ip, _target_port);

    return 0;
}

int relay_push::retry()
{
    CASTER::Withdraw_Rover_Record(_login_mpt.c_str(), _user_name.c_str(), _connect_key.c_str());
    CASTER::Set_Push_Rover_Info(_login_mpt.c_str(), _target_mpt.c_str(), "", 0); // 更新PUSH数据流状态
    // 清理当前上下文
    if (_bev != nullptr)
    {
        bufferevent_free(_bev);
        _bev = nullptr;
    }

    // evbuffer_free(_send_evbuf);
    // evbuffer_free(_recv_evbuf);

    // 设置定时函数，定时函数到期则开始尝试重连

    // 设置一个超时回调

    _reconnect_tv.tv_sec = 5;
    _reconnect_tv.tv_usec = 0;

    // 设置仅激活一次
    event_add(_reconnect_ev, &_reconnect_tv);

    // start();

    return 0;
}

int relay_push::runing()
{
    bufferevent_enable(_bev, EV_READ | EV_WRITE);

    if (_connect_timeout > 0)
    {
        _bev_read_timeout_tv.tv_sec = _connect_timeout;
        _bev_read_timeout_tv.tv_usec = 0;
        bufferevent_set_timeouts(_bev, &_bev_read_timeout_tv, NULL);
    }

    spdlog::info("[{}]: mount [{}] is online, addr:[{}:{}]", __class__, _login_mpt, _target_ip, _target_port);

    if (_timeout_ev_flag == false)
    {
        _timeout_tv.tv_sec = 1;
        _timeout_tv.tv_usec = 0;
        event_add(_timeout_ev, &_timeout_tv);
        _timeout_ev_flag = true;
    }

    CASTER::Set_Pull_Base_Info(_login_mpt.c_str(), _target_mpt.c_str(), _connect_key.c_str(), 1);

    return 0;
}

int relay_push::transfer_sub_raw_data(const char *data, size_t length)
{
    auto UnsendBufferSize = evbuffer_get_length(bufferevent_get_output(_bev));

    if (_unsend_byte_limit > 0 && UnsendBufferSize > _unsend_byte_limit)
    {
        spdlog::info("[{}:{}: send to user [{}]'s date unsend size is too large :[{}], close the connect! using mount [{}], addr:[{}:{}]", __class__, __func__, _user_name, UnsendBufferSize, _login_mpt, _ip, _port);
        retry();
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

int relay_push::publish_recv_raw_data()
{
    size_t length = evbuffer_get_length(_recv_evbuf);
    char *data = new char[length + 1];
    data[length] = '\0';
    evbuffer_remove(_recv_evbuf, data, length);

    CASTER::Pub_Rover_Raw_Data(_user_name.c_str(), _connect_key.c_str(), data, length);
    // _str_decoder.Decode(data, length);
    // if (_str_decoder._has_position)
    // {
    //     CASTER::Set_Rover_Coord_Info(_user_name.c_str(), _connect_key.c_str(),
    //                                  _str_decoder._ecef_x, _str_decoder._ecef_y, _str_decoder._ecef_z,
    //                                  _str_decoder._position_update_time,
    //                                  _str_decoder._quality,
    //                                  _str_decoder._sat_num, _str_decoder._diff);
    // }

    delete[] data;
    return 0;
}

int relay_push::update_push_status_info()
{
    if (_bev != nullptr)
    {
        CASTER::Set_Rover_Delay_Info(_user_name.c_str(), _connect_key.c_str(), util_get_tcp_delay(bufferevent_getfd(_bev)));
    }
    return 0;
}

void relay_push::ConnectedCallback(bufferevent *bev, short events, void *arg)
{
    auto svr = static_cast<relay_push *>(arg);

    // 如果是连接建立成功，发送验证消息
    // 连接建立成功
    if (events == BEV_EVENT_CONNECTED)
    {
        auto fd = bufferevent_getfd(bev);
        svr->_connect_key = util_cal_connect_key(fd);
        svr->_ip = util_get_user_ip(fd);
        svr->_port = util_get_user_port(fd);

        svr->send_login_request();
        return;
    }

    spdlog::info("[{}:{}]: {}{}{}{}{}{}",
                 __class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-");

    // bufferevent_free(bev);
    svr->retry();
    //  如果是连接建立失败，关闭连接，移除
}

void relay_push::VerifyCallback(bufferevent *bev, void *arg)
{
    auto svr = static_cast<relay_push *>(arg);

    bufferevent_disable(bev, EV_READ);              // 暂停/停止接收数据
    bufferevent_setcb(bev, NULL, NULL, NULL, NULL); // 清空bev绑定的回调？  如果这个时候bev event_cb已经激活怎么办?是否就不继续执行了

    bufferevent_set_timeouts(bev, NULL, NULL); // 解绑定时器

    if (svr->verify_login_response())
    {
        spdlog::warn("[{}:{}]: verify login response fail", __class__, __func__);
        bufferevent_free(bev); // 释放bev
        svr->retry();
    }
    else
    {
        spdlog::info("[{}:{}]: verify login response success", __class__, __func__);
        svr->request_new_relay_server();
        bufferevent_disable(bev, EV_READ);
    }
}

void relay_push::ReconnectCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<relay_push *>(arg);

    svr->start();
}

void relay_push::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto svr = static_cast<relay_push *>(arg);

    spdlog::info("[{}:{}]: {}{}{}{}{}{} , mount [{}], addr:[{}:{}]",
                 __class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-", svr->_login_mpt, svr->_target_ip, svr->_target_port);

    svr->retry();
}

void relay_push::ReadCallback(bufferevent *bev, void *arg)
{
    auto svr = static_cast<relay_push *>(arg);
    bufferevent_read_buffer(bev, svr->_recv_evbuf);
    svr->publish_recv_raw_data();
}

void relay_push::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *svr = static_cast<relay_push *>(arg);

    // 定时函数已经被停止，该次调用不处理
    if (svr->_timeout_ev_flag == false)
    {
        return;
    }

    svr->update_push_status_info();
}

void relay_push::Caster_Register_Callback(const char *request, void *arg, catser_reply *reply)
{
    auto svr = static_cast<relay_push *>(arg);
    switch (reply->type)
    {
    case CasterReply::OK:
        CASTER::Sub_Base_Raw_Data(svr->_login_mpt.c_str(), svr->_user_name.c_str(), svr->_connect_key.c_str(), Caster_Sub_Callback, svr);
        break;
    case CasterReply::ERR:
        spdlog::info("[{}:{}]: CASTER_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name.c_str(), svr->_login_mpt, svr->_target_ip, svr->_target_port);
        svr->retry();
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
}

void relay_push::Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply)
{
    auto svr = static_cast<relay_push *>(arg);

    if (reply->type == CasterReply::STRING)
    {
        svr->transfer_sub_raw_data(reply->str, reply->len);
    }
    else if (reply->type == CasterReply::OK)
    {
        spdlog::info("[{}]: user [{}] is login, using mount [{}], addr:[{}:{}]", __class__, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        svr->runing();
    }
    else if (reply->type == CasterReply::ERR)
    {
        spdlog::info("[{}:{}]: CASTER_REPLY_ERR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_login_mpt, svr->_ip, svr->_port);
        svr->retry();
    }
}

int relay_push::send_login_request()
{
    evbuffer *evbuf = bufferevent_get_output(_bev);

    std::string usr_pwd = _target_account + ":" + _target_password;
    std::string userID = util_base64_encode(usr_pwd.c_str());

    if (_type == 2) // Ntrip/2.0
    {
        evbuffer_add_printf(evbuf, "POST /%s HTTP/1.1\r\n", _target_mpt.c_str());
        evbuffer_add_printf(evbuf, "Host: %s\r\n", _target_ip.c_str());
        evbuffer_add_printf(evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
        evbuffer_add_printf(evbuf, "Authorization: Basic %s\r\n", userID.c_str());
        evbuffer_add_printf(evbuf, "User-Agent: Ntrip %s_%s/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
        evbuffer_add_printf(evbuf, "Transfer-Encoding: chunked\r\n"); // 如果使用2.0, 默认使用chunked传输，但具体能不能开启，还要看服务端是否支持
        evbuffer_add_printf(evbuf, "Connection: close\r\n");
        evbuffer_add_printf(evbuf, "\r\n");
    }
    else if (_type == 1) // Ntrip/1.0
    {
        evbuffer_add_printf(evbuf, "SOURCE %s /%s\r\n", _target_password.c_str(), _target_mpt.c_str());
        evbuffer_add_printf(evbuf, "User-Agent: Ntrip %s_%s/1.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
        evbuffer_add_printf(evbuf, "\r\n");
    }

    return 0;
}

int relay_push::verify_login_response()
{

    // 读取回复报文头
    evbuffer *evbuf = bufferevent_get_input(_bev);

    size_t headerlen = 0;
    char *header = evbuffer_readln(evbuf, &headerlen, EVBUFFER_EOL_CRLF_STRICT);

    if (header == NULL | headerlen > 128)
    {
        spdlog::warn("[{}:{}]: error respone", __class__, __func__);
        return 1;
    }

    if (_type == 2) // Ntrip/2.0
    {
        if (strcmp(header, "HTTP/1.1 200 OK"))
        {
            spdlog::warn("[{}:{}]: Unexpected respone", __class__, __func__);
            return 1;
        }

        // 判断是否包含chunked传输
        while (evbuffer_get_length(evbuf) > 0)
        {
            size_t line_len = 0;
            char *line = evbuffer_readln(evbuf, &line_len, EVBUFFER_EOL_CRLF_STRICT);
            if (line == NULL || line_len == 0)
            {
                free(line);
                break;
            }
            if (strcmp(header, "Transfer-Encoding: chunked"))
            {
                _transfer_with_chunked = true;
            }
            free(line);
        }
    }
    else if (_type == 1) // Ntrip/1.0
    {
        if (strcmp(header, "ICY 200 OK"))
        {
            spdlog::warn("[{}:{}]: Unexpected respone", __class__, __func__);
            return 1;
        }
    }

    free(header);
    evbuffer_drain(evbuf, evbuffer_get_length(evbuf));

    return 0;
}

int relay_push::request_new_relay_server()
{

    bufferevent_setcb(_bev, ReadCallback, NULL, EventCallback, this);

    // 验证完成，注册数据流到CasterCore
    CASTER::Register_Rover_Record(_login_mpt.c_str(), _user_name.c_str(), _connect_key.c_str(), Caster_Register_Callback, this, CasterRegisterType::PUSH_USR);
    CASTER::Set_Push_Rover_Info(_login_mpt.c_str(), _target_mpt.c_str(), "", 0);

    return 0;
}
