#include "relay_push.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

relay_push::relay_push(ConnectInfo info)
{
    _info = std::move(info);
    _connect_key = _info.connect_key();
    _mount_point = _info.mount_point();
    _user_name = _info.user_name();
    _ntrip_version2 = _info.ntrip_version() == "Ntrip/2.0";
    _transfer_with_chunked = _info.http_chunked() == "chunked";

    _bev = connect_bev::getInstance()->get_bev(_connect_key);
    _send_evbuf = evbuffer_new();
    _recv_evbuf = evbuffer_new();
    _timeout_ev = event_new(connect_bev::getInstance()->get_base(), -1, EV_PERSIST, TimeoutCallback, this);
}

relay_push::~relay_push()
{
    cleanup_connection();
    connect_bev::getInstance()->del_bev(_info.connect_key());

    if (_send_evbuf)
    {
        evbuffer_free(_send_evbuf);
    }
    if (_recv_evbuf)
    {
        evbuffer_free(_recv_evbuf);
    }
    if (_timeout_ev)
    {
        event_free(_timeout_ev);
    }
}

int relay_push::start()
{
    _stopped = false;
    return start_connect();
}

int relay_push::stop()
{
    if (_stopped)
    {
        return 0;
    }

    _stopped = true;
    if (_timeout_ev_flag)
    {
        event_del(_timeout_ev);
        _timeout_ev_flag = false;
    }
    cleanup_connection();
    _state = State::Idle;

    spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _mount_point, _info.addr(), _info.port());
    return 0;
}

void relay_push::backoff()
{
    _retry_delay = std::min(_retry_delay * 2, 60);
}

void relay_push::reset_backoff()
{
    _retry_delay = 5;
}

int relay_push::start_connect()
{
    if (_stopped)
    {
        return 0;
    }
    if (_timeout_ev_flag)
    {
        event_del(_timeout_ev);
        _timeout_ev_flag = false;
    }

    _state = State::Connecting;
    _connect_key = connect_bev::getInstance()->new_bev(_info.addr(), _info.port());
    if (_connect_key.empty())
    {
        return schedule_retry("create connection failed");
    }

    _bev = connect_bev::getInstance()->get_bev(_connect_key);
    connect_bev::getInstance()->set_bev(_connect_key, ReadCallback, nullptr, EventCallback, this);
    return 0;
}

int relay_push::handle_connected()
{
    auto new_key = connect_bev::getInstance()->recalculate_key(_connect_key);
    if (new_key != _connect_key)
    {
        _connect_key = new_key;
        _bev = connect_bev::getInstance()->get_bev(_connect_key);
    }

    _user_name = "SYSTEM";
    auto target_mpt = _info.mount_para().empty() ? _mount_point : _info.mount_para();
    auto request = build_ntrip_request(CONNECT_TYPE_PUSH, _ntrip_version2, target_mpt, _info.http_host(), _info.ntrip_auth());
    bufferevent_write(_bev, request.data(), request.size());
    _state = State::Handshaking;
    return 0;
}

int relay_push::handle_handshake()
{
    size_t length = evbuffer_get_length(_recv_evbuf);
    if (length == 0)
    {
        return schedule_retry("handshake failed: no data");
    }

    std::vector<char> data(length);
    evbuffer_remove(_recv_evbuf, data.data(), length);

    bool version2 = false;
    bool chunked = false;
    if (!verify_ntrip_response(data.data(), data.size(), version2, chunked))
    {
        return schedule_retry("handshake verify failed");
    }

    _ntrip_version2 = version2;
    _transfer_with_chunked = chunked;
    _state = State::Registering;
    CASTER::Register_Record(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), CasterRegisterCallback, this, _register_type);
    return 0;
}

int relay_push::running()
{
    _subscribed = true;
    _state = State::Running;
    reset_backoff();
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, EventCallback, this);

    spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _mount_point, _info.addr(), _info.port());
    return 0;
}

int relay_push::cleanup_connection()
{
    if (_connect_key.empty())
    {
        return 0;
    }

    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, nullptr, nullptr);
    connect_bev::getInstance()->del_timer(_connect_key);
    if (_subscribed)
    {
        CASTER::Unsub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), _register_type);
        _subscribed = false;
    }
    if (_registered)
    {
        CASTER::Withdraw_Record(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), _register_type);
        _registered = false;
    }

    connect_bev::getInstance()->del_bev(_connect_key);
    _connect_key.clear();
    _bev = nullptr;
    return 0;
}

int relay_push::schedule_retry(const std::string &reason)
{
    if (_stopped)
    {
        return 0;
    }

    spdlog::warn("[{}]: {}, retry in {}s, mount [{}], addr:[{}:{}]", __class__, reason, _retry_delay, _mount_point, _info.addr(), _info.port());
    cleanup_connection();
    _state = State::WaitingRetry;
    _timeout_tv.tv_sec = _retry_delay;
    _timeout_tv.tv_usec = 0;
    event_add(_timeout_ev, &_timeout_tv);
    _timeout_ev_flag = true;
    backoff();
    return 0;
}

int relay_push::transfer_sub_raw_data(const char *data, size_t length)
{
    if (_transfer_with_chunked)
    {
        evbuffer_add_printf(_send_evbuf, "%lx\r\n", length);
        evbuffer_add(_send_evbuf, data, length);
        evbuffer_add(_send_evbuf, "\r\n", 2);
    }
    else
    {
        evbuffer_add(_send_evbuf, data, length);
    }

    bufferevent_write_buffer(_bev, _send_evbuf);
    return 0;
}

void relay_push::ReadCallback(bufferevent *bev, void *arg)
{
    auto *session = static_cast<relay_push *>(arg);
    bufferevent_read_buffer(bev, session->_recv_evbuf);
    if (session->_state == State::Handshaking)
    {
        session->handle_handshake();
    }
}

void relay_push::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto *session = static_cast<relay_push *>(arg);
    spdlog::info("[{}:{}]: {}{}{}{}{}{}, mount [{}], addr:[{}:{}]",
                 session->__class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-",
                 session->_mount_point, session->_info.addr(), session->_info.port());

    if (session->_stopped)
    {
        return;
    }
    if (session->_state == State::Connecting && (events & BEV_EVENT_CONNECTED))
    {
        session->handle_connected();
        return;
    }
    session->schedule_retry("disconnected");
}

void relay_push::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *session = static_cast<relay_push *>(arg);
    if (session->_state != State::WaitingRetry || session->_stopped)
    {
        return;
    }

    event_del(session->_timeout_ev);
    session->_timeout_ev_flag = false;
    session->start_connect();
}

void relay_push::CasterRegisterCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *session = static_cast<relay_push *>(arg);
    if (reply->type == CasterReply::OK)
    {
        session->_registered = true;
        session->_state = State::Subscribing;
        CASTER::Sub_Raw_Data(session->_connect_key.c_str(), session->_mount_point.c_str(), session->_user_name.c_str(), CasterSubscribeCallback, session, session->_register_type);
    }
    else if (reply->type == CasterReply::ERR)
    {
        session->schedule_retry("caster register failed/kicked");
    }
}

void relay_push::CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *session = static_cast<relay_push *>(arg);
    if (reply->type == CasterReply::STRING)
    {
        if (session->_state == State::Running)
        {
            session->transfer_sub_raw_data(reply->str, reply->len);
        }
    }
    else if (reply->type == CasterReply::OK)
    {
        if (session->_state == State::Subscribing)
        {
            session->running();
        }
    }
    else if (reply->type == CasterReply::ERR)
    {
        session->schedule_retry("subscribe failed/kicked");
    }
}