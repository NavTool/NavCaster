#include "carrier_base.h"
#include "knt.h"
#include "base64.h"

#define __class__ "carrier_base"

carrier_base::carrier_base()
{
    // _info = info;
    // _bev = bev;
}

carrier_base::~carrier_base()
{
}

int carrier_base::start()
{
    return 0;
}

int carrier_base::stop()
{
    return 0;
}

int carrier_base::update()
{
    return 0;
}

int carrier_base::pause()
{
    return 0;
}

int carrier_base::unpause()
{
    return 0;
}

int carrier_base::running()
{
    return 0;
}


int carrier_base::retry()
{
    return 0;
}

void carrier_base::Auth_Login_Callback(const char *request, void *arg, auth_reply *reply)
{
    auto svr = static_cast<carrier_base *>(arg);

    switch (reply->type)
    {
    case AuthReply::OK:
        CASTER::Register_Rover_Record(svr->_mount_point.c_str(), svr->_user_name.c_str(), svr->_connect_key.c_str(), Caster_Register_Callback, svr, CasterRegisterType::NORMAL);
        break;
    case AuthReply::ERR:
        spdlog::info("[{}:{}]: AUTH_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->stop();
        break;
    default:
        break;
    }
}

void carrier_base::Caster_Register_Callback(const char *request, void *arg, catser_reply *reply)
{
    auto svr = static_cast<carrier_base *>(arg);
    switch (reply->type)
    {
    case CasterReply::OK:
        CASTER::Sub_Base_Raw_Data(svr->_mount_point.c_str(), svr->_user_name.c_str(), svr->_connect_key.c_str(), Caster_Sub_Callback, svr);
        break;
    case CasterReply::ERR:
        spdlog::info("[{}:{}]: CASTER_REPLY_ERROR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->stop();
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

void carrier_base::Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply)
{
    auto svr = static_cast<carrier_base *>(arg);

    if (reply->type == CasterReply::STRING)
    {
        svr->transfer_sub_raw_data(reply->str, reply->len);
    }
    else if (reply->type == CasterReply::OK)
    {
        spdlog::info("[{}]: user [{}] is login, using mount [{}], addr:[{}:{}]", __class__, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->runing();
    }
    else if (reply->type == CasterReply::ERR)
    {
        spdlog::info("[{}:{}]: CASTER_REPLY_ERR:[{}], user [{}] , using mount [{}], addr:[{}:{}]", __class__, __func__, reply->str, svr->_user_name, svr->_mount_point, svr->_ip, svr->_port);
        svr->stop();
    }
}

void carrier_base::ReadCallback(bufferevent *bev, void *arg)
{
}

void carrier_base::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto svr = static_cast<carrier_base *>(arg);

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

void carrier_base::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
}

void carrier_base::TimeoutCallback(intptr_t fd, short events, void *arg)
{
    auto *svr = static_cast<carrier_base *>(arg);
    // 定时函数已经被停止，该次调用不处理
    if (svr->_timeout_ev_flag == false)
    {
        return;
    }
    // svr->send_heart_beat_to_server();
    svr->update_tcp_delay_info();
}

int carrier_base::update_tcp_delay_info()
{
    return CASTER::Set_Rover_Delay_Info(_user_name.c_str(), _connect_key.c_str(), util_get_tcp_delay(bufferevent_getfd(_bev)));
}

int carrier_base::bev_send_reply(ConnectType type, bool version2, bool chuncked)
{
    if (type == CONNECT_TYPE_SERVER)
    {
        if (version2)
        {
            evbuffer_add_printf(_send_evbuf, "HTTP/1.1 200 OK\r\n");
            evbuffer_add_printf(_send_evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
            evbuffer_add_printf(_send_evbuf, "Server: Ntrip %s_%s/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            evbuffer_add_printf(_send_evbuf, "Date: %s\r\n", util_get_http_date().c_str());
            if (chuncked)
            {
                evbuffer_add_printf(_send_evbuf, "Transfer-Encoding: chunked\r\n");
            }
            evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
        else
        {
            evbuffer_add_printf(_send_evbuf, "ICY 200 OK\r\n");
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
    }
    else if (type == CONNECT_TYPE_CLIENT)
    {
        if (version2)
        {
            evbuffer_add_printf(_send_evbuf, "HTTP/1.1 200 OK\r\n");
            evbuffer_add_printf(_send_evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
            evbuffer_add_printf(_send_evbuf, "Server: Ntrip %s_%s/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            evbuffer_add_printf(_send_evbuf, "Date: %s\r\n", util_get_http_date().c_str());
            evbuffer_add_printf(_send_evbuf, "Cache-Control: no-store, no-cache, max-age=0\r\n");
            evbuffer_add_printf(_send_evbuf, "Pragma: no-cache\r\n");
            evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
            if (chuncked)
            {
                evbuffer_add_printf(_send_evbuf, "Transfer-Encoding: chunked\r\n");
            }
            evbuffer_add_printf(_send_evbuf, "Content-Type: gnss/data\r\n");
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
        else
        {
            evbuffer_add_printf(_send_evbuf, "ICY 200 OK\r\n");
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
    }
    else
    {
        return 1;
    }
    // if (type == CONNECT_TYPE_SOURCE)
    // {
    // }

    bufferevent_write_buffer(_bev, _send_evbuf);
    return 0;
}

int carrier_base::bev_send_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth)
{
    if (type == CONNECT_TYPE_PULL)
    {
        if (version2) // Ntrip/2.0
        {
            evbuffer_add_printf(_send_evbuf, "GET %s HTTP/1.1\r\n", mpt.c_str());
            evbuffer_add_printf(_send_evbuf, "Host: %s\r\n", host.c_str());
            evbuffer_add_printf(_send_evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
            evbuffer_add_printf(_send_evbuf, "User-Agent: %s/%s\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            evbuffer_add_printf(_send_evbuf, "Authorization: Basic %s\r\n", auth.c_str());
            evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
        else // Ntrip/1.0
        {
            evbuffer_add_printf(_send_evbuf, "GET %s HTTP/1.0\r\n", mpt.c_str());
            evbuffer_add_printf(_send_evbuf, "User-Agent: %s/%s\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            evbuffer_add_printf(_send_evbuf, "Authorization: Basic %s\r\n", auth.c_str());
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
    }
    else if (type == CONNECT_TYPE_PUSH)
    {
        if (version2) // Ntrip/2.0
        {
            evbuffer_add_printf(_send_evbuf, "POST /%s HTTP/1.1\r\n", mpt.c_str());
            evbuffer_add_printf(_send_evbuf, "Host: %s\r\n", host.c_str());
            evbuffer_add_printf(_send_evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
            evbuffer_add_printf(_send_evbuf, "Authorization: Basic %s\r\n", auth.c_str());
            evbuffer_add_printf(_send_evbuf, "User-Agent: Ntrip %s_%s/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            evbuffer_add_printf(_send_evbuf, "Transfer-Encoding: chunked\r\n"); // 如果使用2.0, 默认使用chunked传输，但具体能不能开启，还要看服务端是否支持
            evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
        else // Ntrip/1.0
        {
            evbuffer_add_printf(_send_evbuf, "SOURCE %s /%s\r\n", auth.c_str(), mpt.c_str());
            evbuffer_add_printf(_send_evbuf, "User-Agent: Ntrip %s_%s/1.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            evbuffer_add_printf(_send_evbuf, "\r\n");
        }
    }
    else
    {
        return 1;
    }

    bufferevent_write_buffer(_bev, _send_evbuf);

    return 0;
}

int carrier_base::create_bev(std::string ip, int port)
{
    return 0;
}

int carrier_base::init_bev(bufferevent *bev)
{
    return 0;
}

int carrier_base::set_bev(bool enable_read, time_t read_timeout_ms, bool enable_write, time_t write_timeout_ms, bool enable_event)
{
    return 0;
}

int carrier_base::free_bev()
{
    return 0;
}

bool carrier_base::set_timeout(time_t time_ms)
{
    return false;
}

void carrier_base::process_recv_data(const char *data, size_t length)
{
}

void carrier_base::process_send_data(const char *data, size_t length)
{
}

void carrier_base::process_event(int type)
{
}

void carrier_base::process_timeout()
{
}
