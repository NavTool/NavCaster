#include "server_ntrip.h"

#include "Caster_Core.h"
#include "ntrip_config.h"
#include "process_queue.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

server_ntrip::server_ntrip(ConnectInfo info)
{
    _info = std::move(info);
    _connect_key = _info.connect_key();
    _mount_point = _info.mount_point();
    _user_name = _info.user_name();
    _group_uid = _info.group_uid().empty() ? "default" : _info.group_uid();
    _ntrip_version2 = _info.ntrip_version() == "Ntrip/2.0";
    _transfer_with_chunked = _info.http_chunked() == "chunked";

    _bev = connect_bev::getInstance()->get_bev(_connect_key);
    _send_evbuf = evbuffer_new();
    _recv_evbuf = evbuffer_new();
    _timeout_ev = event_new(connect_bev::getInstance()->get_base(), -1, EV_PERSIST, TimeoutCallback, this);
}

server_ntrip::~server_ntrip()
{
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

int server_ntrip::start()
{
    _stopped = false;
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, EventCallback, this);
    const auto runtime = auth_runtime_context();
    AUTH::Add_Login_Record(_user_name.c_str(), _connect_key.c_str(), AuthLoginCallback, this, _auth_type, &runtime);
    return 0;
}

int server_ntrip::stop()
{
    if (_stopped)
    {
        return 0;
    }

    _stopped = true;
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, nullptr, nullptr);
    connect_bev::getInstance()->del_timer(_connect_key);

    if (_timeout_ev_flag)
    {
        event_del(_timeout_ev);
        _timeout_ev_flag = false;
    }
    const auto runtime = auth_runtime_context("client_closed");
    AUTH::Add_Logout_Record(_user_name.c_str(), _connect_key.c_str(), _auth_type, &runtime);
    if (_registered)
    {
        CASTER::Withdraw_Record(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), _register_type);
        _registered = false;
    }

    _info.set_operate(OPERATE_TYPE_DESTROY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _mount_point, _info.addr(), _info.port());
    return 0;
}

int server_ntrip::running()
{
    auto *conf = ntrip_config::getInstance();
    connect_bev::getInstance()->set_bev(_connect_key, ReadCallback, nullptr, EventCallback, this);
    connect_bev::getInstance()->set_timer(_connect_key, conf->_ntrip_server_opt.connect_timeout(), 0);

    auto heartbeat_interval = conf->_ntrip_server_opt.heartbeat_interval();
    if (heartbeat_interval > 0 && !_timeout_ev_flag)
    {
        _timeout_tv.tv_sec = heartbeat_interval;
        _timeout_tv.tv_usec = 0;
        event_add(_timeout_ev, &_timeout_tv);
        _timeout_ev_flag = true;
    }

    send_reply();
    spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _mount_point, _info.addr(), _info.port());
    return 0;
}

int server_ntrip::send_reply()
{
    auto reply = build_nrtip_reply(CONNECT_TYPE_SERVER, _ntrip_version2, _transfer_with_chunked);
    bufferevent_write(_bev, reply.data(), reply.size());
    return 0;
}

AuthRuntimeContext server_ntrip::auth_runtime_context(const char *reason) const
{
    AuthRuntimeContext runtime;
    runtime.connect_key = _connect_key;
    runtime.mountpoint = _mount_point;
    runtime.addr = _info.addr();
    runtime.port = _info.port();
    runtime.user_agent = _info.user_agent();
    runtime.ntrip_version = _info.ntrip_version();
    runtime.node_id = CASTER::Get_Node_ID();
    runtime.disconnect_reason = reason && *reason ? reason : "client_closed";
    return runtime;
}

int server_ntrip::send_heart_beat_to_server()
{
    auto *conf = ntrip_config::getInstance();
    auto unsend_size = evbuffer_get_length(bufferevent_get_output(_bev));
    auto unsend_limit = conf->_ntrip_server_opt.unsend_byte_limit();

    if (unsend_limit > 0 && unsend_size > static_cast<size_t>(unsend_limit))
    {
        spdlog::warn("[{}]: unsend size too large: {}, mount [{}], addr:[{}:{}]", __class__, unsend_size, _mount_point, _info.addr(), _info.port());
        return stop();
    }

    auto heartbeat_msg = conf->_ntrip_server_opt.heartbeat_msg();
    if (!heartbeat_msg.empty())
    {
        bufferevent_write(_bev, heartbeat_msg.data(), heartbeat_msg.size());
    }
    return 0;
}

int server_ntrip::publish_recv_raw_data()
{
    return _transfer_with_chunked ? publish_data_from_chunk() : publish_data_from_evbuf();
}

int server_ntrip::publish_data_from_evbuf()
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

int server_ntrip::publish_data_from_chunk()
{
    if (_chunked_size == 0)
    {
        size_t chunk_head_size = 0;
        char *chunk_head_data = evbuffer_readln(_recv_evbuf, &chunk_head_size, EVBUFFER_EOL_CRLF);
        if (!chunk_head_data)
        {
            spdlog::warn("[{}:{}]: chunked data error, close connect, mount [{}], addr:[{}:{}]", __class__, __func__, _mount_point, _info.addr(), _info.port());
            return stop();
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

void server_ntrip::ReadCallback(bufferevent *bev, void *arg)
{
    auto *session = static_cast<server_ntrip *>(arg);
    bufferevent_read_buffer(bev, session->_recv_evbuf);
    session->publish_recv_raw_data();
}

void server_ntrip::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto *session = static_cast<server_ntrip *>(arg);
    spdlog::info("[{}:{}]: {}{}{}{}{}{}, mount [{}], addr:[{}:{}]",
                 session->__class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-",
                 session->_mount_point, session->_info.addr(), session->_info.port());
    session->stop();
}

void server_ntrip::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *session = static_cast<server_ntrip *>(arg);
    session->send_heart_beat_to_server();
}

void server_ntrip::AuthLoginCallback(const char *request, void *arg, auth_reply *reply)
{
    auto *session = static_cast<server_ntrip *>(arg);
    if (reply->type == AuthReply::OK)
    {
        session->_group_uid = reply->group_uid.empty() ? session->_group_uid : reply->group_uid;
        CASTER::Register_Record(session->_connect_key.c_str(), session->_mount_point.c_str(), session->_user_name.c_str(), CasterRegisterCallback, session, session->_register_type, session->_group_uid.c_str());
        return;
    }

    spdlog::warn("[{}]: auth failed/kicked, mount [{}], addr:[{}:{}]", session->__class__, session->_mount_point, session->_info.addr(), session->_info.port());
    session->stop();
}

void server_ntrip::CasterRegisterCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *session = static_cast<server_ntrip *>(arg);
    if (reply->type == CasterReply::OK)
    {
        session->_registered = true;
        session->running();
        return;
    }

    if (reply->type == CasterReply::ERR)
    {
        spdlog::warn("[{}]: caster failed/kicked, mount [{}], addr:[{}:{}]", session->__class__, session->_mount_point, session->_info.addr(), session->_info.port());
        session->stop();
    }
}
