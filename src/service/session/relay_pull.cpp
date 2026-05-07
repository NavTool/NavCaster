#include "relay_pull.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

relay_pull::relay_pull(ConnectInfo info)
{
    _info = std::move(info);
    _task_key = _info.connect_key();
    _mount_point = _info.mount_point();
    _user_name = _info.user_name();
    _ntrip_version2 = _info.ntrip_version() == "Ntrip/2.0";
    _transfer_with_chunked = _info.http_chunked() == "chunked";

    // _bev = connect_bev::getInstance()->get_bev(_connect_key);
    _recv_evbuf = evbuffer_new();
    _timeout_ev = event_new(connect_bev::getInstance()->get_base(), -1, EV_PERSIST, TimeoutCallback, this);
}

relay_pull::~relay_pull()
{
    cleanup_connection();

    if (_recv_evbuf)
    {
        evbuffer_free(_recv_evbuf);
    }
    if (_timeout_ev)
    {
        event_free(_timeout_ev);
    }
}

int relay_pull::start()
{
    _stopped = false;
    return start_connect();
}

int relay_pull::stop()
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

void relay_pull::backoff()
{
    _retry_delay = std::min(_retry_delay * 2, 60);
}

void relay_pull::reset_backoff()
{
    _retry_delay = 5;
}

int relay_pull::start_connect()
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

int relay_pull::handle_connected()
{
    auto new_key = connect_bev::getInstance()->recalculate_key(_connect_key);
    if (new_key != _connect_key)
    {
        _connect_key = new_key;
        _bev = connect_bev::getInstance()->get_bev(_connect_key);
    }

    _user_name = "SYSTEM";
    auto target_mpt = _info.mount_para().empty() ? _mount_point : _info.mount_para();
    auto request = build_ntrip_request(CONNECT_TYPE_PULL, _ntrip_version2, target_mpt, _info.http_host(), _info.ntrip_auth());
    bufferevent_write(_bev, request.data(), request.size());
    _state = State::Handshaking;
    return 0;
}

int relay_pull::handle_handshake()
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

int relay_pull::running()
{
    _registered = true;
    _state = State::Running;
    reset_backoff();
    connect_bev::getInstance()->set_bev(_connect_key, ReadCallback, nullptr, EventCallback, this);
    CASTER::Set_Pull_Base_Info(_task_key.c_str(), _mount_point.c_str(), _connect_key.c_str(), 1);

    spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _mount_point, _info.addr(), _info.port());
    return 0;
}

int relay_pull::cleanup_connection()
{
    if (_connect_key.empty())
    {
        return 0;
    }

    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, nullptr, nullptr);
    connect_bev::getInstance()->del_timer(_connect_key);
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

int relay_pull::schedule_retry(const std::string &reason)
{
    if (_stopped)
    {
        return 0;
    }

    spdlog::warn("[{}]: {}, retry in {}s, mount [{}], addr:[{}:{}]", __class__, reason, _retry_delay, _mount_point, _info.addr(), _info.port());
    cleanup_connection();
    CASTER::Set_Pull_Base_Info(_task_key.c_str(), _mount_point.c_str(), "", 0);
    _state = State::WaitingRetry;
    _timeout_tv.tv_sec = _retry_delay;
    _timeout_tv.tv_usec = 0;
    event_add(_timeout_ev, &_timeout_tv);
    _timeout_ev_flag = true;
    backoff();
    return 0;
}

int relay_pull::publish_recv_raw_data()
{
    return _transfer_with_chunked ? publish_data_from_chunk() : publish_data_from_evbuf();
}

int relay_pull::publish_data_from_evbuf()
{
    size_t length = evbuffer_get_length(_recv_evbuf);
    if (length == 0)
    {
        return 0;
    }

    std::vector<char> data(length);
    evbuffer_remove(_recv_evbuf, data.data(), length);
    CASTER::Pub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), data.data(), length, _register_type);
    return 0;
}

int relay_pull::publish_data_from_chunk()
{
    if (_chunked_size == 0)
    {
        size_t chunk_head_size = 0;
        char *chunk_head_data = evbuffer_readln(_recv_evbuf, &chunk_head_size, EVBUFFER_EOL_CRLF);
        if (!chunk_head_data)
        {
            return schedule_retry("chunked data error");
        }
        std::sscanf(chunk_head_data, "%zx", &chunk_head_size);
        std::free(chunk_head_data);
        _chunked_size = chunk_head_size;
    }

    size_t length = evbuffer_get_length(_recv_evbuf);
    if (_chunked_size + 2 > length)
    {
        return 0;
    }

    std::vector<char> data(_chunked_size);
    evbuffer_remove(_recv_evbuf, data.data(), _chunked_size);
    evbuffer_drain(_recv_evbuf, 2);
    CASTER::Pub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), data.data(), data.size(), _register_type);
    _chunked_size = 0;

    if (evbuffer_get_length(_recv_evbuf) > 0)
    {
        return publish_data_from_chunk();
    }
    return 0;
}

void relay_pull::ReadCallback(bufferevent *bev, void *arg)
{
    auto *session = static_cast<relay_pull *>(arg);
    bufferevent_read_buffer(bev, session->_recv_evbuf);

    if (session->_state == State::Handshaking)
    {
        session->handle_handshake();
    }
    else if (session->_state == State::Running)
    {
        session->publish_recv_raw_data();
    }
}

void relay_pull::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto *session = static_cast<relay_pull *>(arg);
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

void relay_pull::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *session = static_cast<relay_pull *>(arg);
    if (session->_state != State::WaitingRetry || session->_stopped)
    {
        return;
    }

    event_del(session->_timeout_ev);
    session->_timeout_ev_flag = false;
    session->start_connect();
}

void relay_pull::CasterRegisterCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *session = static_cast<relay_pull *>(arg);
    if (reply->type == CasterReply::OK)
    {
        session->running();
    }
    else if (reply->type == CasterReply::ERR)
    {
        session->schedule_retry("caster register failed/kicked");
    }
}