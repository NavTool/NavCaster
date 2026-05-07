#include "source_ntrip.h"

#include "knt.h"
#include "process_queue.h"
#include "version.h"

#include <utility>

#include <spdlog/spdlog.h>

source_ntrip::source_ntrip(ConnectInfo info)
{
    _info = std::move(info);
    _connect_key = _info.connect_key();
    _user_name = _info.user_name();
    _group_uid = _info.group_uid().empty() ? "default" : _info.group_uid();
    _ntrip_version2 = _info.ntrip_version() == "Ntrip/2.0";

    _bev = connect_bev::getInstance()->get_bev(_connect_key);
    _send_evbuf = evbuffer_new();
}

source_ntrip::~source_ntrip()
{
    connect_bev::getInstance()->del_bev(_info.connect_key());
    if (_send_evbuf)
    {
        evbuffer_free(_send_evbuf);
    }
}

int source_ntrip::start()
{
    _stopped = false;
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, WriteCallback, EventCallback, this);

    _source_list = CASTER::Get_Source_Table_Text(_group_uid.c_str());
    return build_source_table();
}

int source_ntrip::stop()
{
    if (_stopped)
    {
        return 0;
    }

    _stopped = true;
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, nullptr, nullptr);
    connect_bev::getInstance()->del_timer(_connect_key);

    _info.set_operate(OPERATE_TYPE_DESTROY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, user [{}], addr:[{}:{}]", __class__, _user_name, _info.addr(), _info.port());
    return 0;
}

void source_ntrip::WriteCallback(bufferevent *bev, void *arg)
{
    auto *session = static_cast<source_ntrip *>(arg);
    auto length = evbuffer_get_length(session->_send_evbuf);
    if (length == 0)
    {
        auto unsend_size = evbuffer_get_length(bufferevent_get_output(bev));
        if (unsend_size == 0)
        {
            spdlog::info("[{}]: Send SourceTable Finished, user [{}], addr:[{}:{}]", session->__class__, session->_user_name, session->_info.addr(), session->_info.port());
            session->stop();
        }
        return;
    }

    bufferevent_write_buffer(bev, session->_send_evbuf);
}

void source_ntrip::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto *session = static_cast<source_ntrip *>(arg);
    spdlog::info("[{}:{}]: {}{}{}{}{}{}, user [{}], addr:[{}:{}]",
                 session->__class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-",
                 session->_user_name, session->_info.addr(), session->_info.port());
    session->stop();
}

int source_ntrip::build_source_table()
{
    if (_ntrip_version2)
    {
        evbuffer_add_printf(_send_evbuf, "HTTP/1.1 200 OK\r\n");
        evbuffer_add_printf(_send_evbuf, "Ntrip-Version: Ntrip/2.0\r\n");
        evbuffer_add_printf(_send_evbuf, "Ntrip-Flags: \r\n");
        evbuffer_add_printf(_send_evbuf, "Server: NTRIP CasterService_%s/2.0\r\n", PROJECT_TAG_VERSION);
        evbuffer_add_printf(_send_evbuf, "Date: %s\r\n", util_get_http_date().c_str());
        evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
        evbuffer_add_printf(_send_evbuf, "Content-Type: gnss/sourcetable\r\n");
        evbuffer_add_printf(_send_evbuf, "Content-Length: %zu\r\n", _source_list.size() + 16);
        evbuffer_add_printf(_send_evbuf, "\r\n");
        evbuffer_add(_send_evbuf, _source_list.c_str(), _source_list.size());
        evbuffer_add_printf(_send_evbuf, "ENDSOURCETABLE\r\n");
    }
    else
    {
        evbuffer_add_printf(_send_evbuf, "SOURCETABLE 200 OK\r\n");
        evbuffer_add_printf(_send_evbuf, "Server: NTRIP CasterService_%s/1.0\r\n", PROJECT_TAG_VERSION);
        evbuffer_add_printf(_send_evbuf, "Date: %s\r\n", util_get_http_date().c_str());
        evbuffer_add_printf(_send_evbuf, "Connection: close\r\n");
        evbuffer_add_printf(_send_evbuf, "Content-Type: text/plain\r\n");
        evbuffer_add_printf(_send_evbuf, "Content-Length: %zu\r\n", _source_list.size() + 16);
        evbuffer_add_printf(_send_evbuf, "\r\n");
        evbuffer_add(_send_evbuf, _source_list.c_str(), _source_list.size());
        evbuffer_add_printf(_send_evbuf, "ENDSOURCETABLE\r\n");
    }
    return 0;
}