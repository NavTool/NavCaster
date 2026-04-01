#include "source_ntrip.h"
#include "knt.h"

source_ntrip::source_ntrip(ConnectInfo info) : carrier_base(info)
{
    __class__ = "source_ntrip";
}

source_ntrip::~source_ntrip()
{
}

int source_ntrip::init()
{
    return 0;
}

// ============ 流程：start直接发送源表 → write_cb检查发送完成 → stop ============

int source_ntrip::start()
{
    start_bev(false, 0, true, 0);

    _source_list = CASTER::Get_Source_Table_Text();
    build_source_table();

    return 0;
}

int source_ntrip::stop()
{
    stop_bev();

    _info.set_operate(OPERATE_TYPE_DESTORY);
    QUEUE::Push(_info);

    spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
    return 0;
}

int source_ntrip::read_cb(bufferevent *bev)
{
    return 0;
}

int source_ntrip::write_cb(bufferevent *bev)
{
    auto length = evbuffer_get_length(_send_evbuf);
    if (length == 0)
    {
        auto UnsendBufferSize = evbuffer_get_length(bufferevent_get_output(bev));
        if (UnsendBufferSize == 0)
        {
            spdlog::info("[{}]: Send SourceTable Finished, user [{}] ,  addr:[{}:{}]", __class__, _info.user_name(), _info.addr(), _info.port());
            stop();
        }
    }
    else
    {
        bufferevent_write_buffer(bev, _send_evbuf);
    }
    return 0;
}

int source_ntrip::event_cb(bufferevent *bev, short events)
{
    spdlog::info("[{}:{}]: event stop, mount [{}], addr:[{}:{}]", __class__, __func__, _info.mount_point(), _info.addr(), _info.port());
    stop();
    return 0;
}

int source_ntrip::timeout_cb()
{
    return 0;
}

int source_ntrip::login_cb(auth_reply *reply)
{
    return 0;
}

int source_ntrip::register_cb(caster_reply *reply)
{
    return 0;
}

int source_ntrip::subscribe_cb(caster_reply *reply)
{
    return 0;
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
        evbuffer_add_printf(_send_evbuf, "Content-Length: %ld\r\n", _source_list.size() + 16);
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
        evbuffer_add_printf(_send_evbuf, "Content-Length: %ld\r\n", _source_list.size() + 16);
        evbuffer_add_printf(_send_evbuf, "\r\n");
        evbuffer_add(_send_evbuf, _source_list.c_str(), _source_list.size());
        evbuffer_add_printf(_send_evbuf, "ENDSOURCETABLE\r\n");
    }
    return 0;
}
