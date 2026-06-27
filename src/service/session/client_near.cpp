#include "client_near.h"

#include "Caster_Core.h"
#include "ntrip_config.h"
#include "process_queue.h"

#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

client_near::client_near(ConnectInfo info)
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
}

client_near::~client_near()
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
}

int client_near::start()
{
    _stopped = false;
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, EventCallback, this);
    const auto runtime = auth_runtime_context();
    AUTH::Add_Login_Record(_user_name.c_str(), _connect_key.c_str(), AuthLoginCallback, this, _auth_type, &runtime);
    return 0;
}

int client_near::stop()
{
    if (_stopped)
    {
        return 0;
    }

    _stopped = true;
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, nullptr, nullptr);
    connect_bev::getInstance()->del_timer(_connect_key);

    const auto runtime = auth_runtime_context("client_closed");
    AUTH::Add_Logout_Record(_user_name.c_str(), _connect_key.c_str(), _auth_type, &runtime);
    if (_registered)
    {
        CASTER::Unsub_Near_Raw_Data(_connect_key.c_str());
        CASTER::Withdraw_Record(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), _register_type);
        _registered = false;
    }

    _info.set_operate(OPERATE_TYPE_DESTROY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, user [{}], mount [{}], addr:[{}:{}]", __class__, _user_name, _mount_point, _info.addr(), _info.port());
    return 0;
}

int client_near::running()
{
    if (_running)
    {
        return 0;
    }

    _running = true;
    if (_bev)
    {
        evbuffer *input = bufferevent_get_input(_bev);
        size_t trailing_len = evbuffer_get_length(input);
        if (trailing_len > 0)
        {
            std::vector<char> data(trailing_len);
            evbuffer_remove(input, data.data(), trailing_len);
            CASTER::Pub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), data.data(), trailing_len, _register_type);
        }
    }

    auto *conf = ntrip_config::getInstance();
    connect_bev::getInstance()->set_bev(_connect_key, ReadCallback, nullptr, EventCallback, this);
    connect_bev::getInstance()->set_timer(_connect_key, conf->_ntrip_client_opt.connect_timeout(), 0);
    send_reply();
    return 0;
}

int client_near::send_reply()
{
    auto reply = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);
    bufferevent_write(_bev, reply.data(), reply.size());
    spdlog::info("[{}]: running, user [{}], mount [{}], addr:[{}:{}]", __class__, _user_name, _mount_point, _info.addr(), _info.port());
    return 0;
}

AuthRuntimeContext client_near::auth_runtime_context(const char *reason) const
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

int client_near::transfer_sub_raw_data(const char *data, size_t length)
{
    auto *conf = ntrip_config::getInstance();
    auto unsend_size = evbuffer_get_length(bufferevent_get_output(_bev));
    auto unsend_limit = conf->_ntrip_client_opt.unsend_byte_limit();

    if (unsend_limit > 0 && unsend_size > static_cast<size_t>(unsend_limit))
    {
        spdlog::warn("[{}]: unsend size too large: {}, user [{}], mount [{}], addr:[{}:{}]", __class__, unsend_size, _user_name, _mount_point, _info.addr(), _info.port());
        return stop();
    }

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

int client_near::publish_recv_raw_data()
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

int client_near::subscribe_initial_position()
{
    double init_lat = _info.ntrip_lat();
    double init_lon = _info.ntrip_lon();

    if (init_lat != 0 || init_lon != 0)
    {
        spdlog::info("[{}]: initial GGA position: lat={:.6f}, lon={:.6f}, mount [{}]", __class__, init_lat, init_lon, _mount_point);
    }
    else
    {
        spdlog::info("[{}]: wait GGA position before nearest query, mount [{}], user [{}]", __class__, _mount_point, _user_name);
    }

    CASTER::Sub_Near_Raw_Data(_mount_point.c_str(), init_lat, init_lon, _user_name.c_str(), _connect_key.c_str(), CasterSubscribeCallback, this, _group_uid.c_str());
    return 0;
}

void client_near::ReadCallback(bufferevent *bev, void *arg)
{
    auto *session = static_cast<client_near *>(arg);
    bufferevent_read_buffer(bev, session->_recv_evbuf);
    session->publish_recv_raw_data();
}

void client_near::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto *session = static_cast<client_near *>(arg);
    spdlog::info("[{}:{}]: {}{}{}{}{}{}, user [{}], mount [{}], addr:[{}:{}]",
                 session->__class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-",
                 session->_user_name, session->_mount_point, session->_info.addr(), session->_info.port());
    session->stop();
}

void client_near::AuthLoginCallback(const char *request, void *arg, auth_reply *reply)
{
    auto *session = static_cast<client_near *>(arg);
    if (reply->type == AuthReply::OK)
    {
        session->_group_uid = reply->group_uid.empty() ? session->_group_uid : reply->group_uid;
        CASTER::Register_Record(session->_connect_key.c_str(), session->_mount_point.c_str(), session->_user_name.c_str(), CasterRegisterCallback, session, session->_register_type, session->_group_uid.c_str());
        return;
    }

    spdlog::warn("[{}]: auth failed/kicked, user [{}], mount [{}], addr:[{}:{}]", session->__class__, session->_user_name, session->_mount_point, session->_info.addr(), session->_info.port());
    session->stop();
}

void client_near::CasterRegisterCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *session = static_cast<client_near *>(arg);
    if (reply->type == CasterReply::OK)
    {
        session->_registered = true;
        session->subscribe_initial_position();
        session->running();
        return;
    }

    if (reply->type == CasterReply::ERR)
    {
        spdlog::warn("[{}]: caster failed/kicked, user [{}], mount [{}], addr:[{}:{}]", session->__class__, session->_user_name, session->_mount_point, session->_info.addr(), session->_info.port());
        session->stop();
    }
}

void client_near::CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *session = static_cast<client_near *>(arg);
    if (reply->type == CasterReply::STRING)
    {
        session->transfer_sub_raw_data(reply->str, reply->len);
    }
    else if (reply->type == CasterReply::OK)
    {
        if (reply->str)
        {
            if (!session->_alias_mpt.empty() && session->_alias_mpt != reply->str)
            {
                spdlog::info("[{}]: nearest base switched [{} -> {}], user [{}], addr:[{}:{}]",
                             session->__class__, session->_alias_mpt, reply->str,
                             session->_user_name, session->_info.addr(), session->_info.port());
            }
            session->_alias_mpt = reply->str;
        }
    }
    else if (reply->type == CasterReply::ERR)
    {
        spdlog::warn("[{}]: subscribe failed/kicked, user [{}], mount [{}], addr:[{}:{}]", session->__class__, session->_user_name, session->_mount_point, session->_info.addr(), session->_info.port());
        session->stop();
    }
}
