/*
    source_ntrip.h — 协程版本的源表 Carrier（混合设计）
    流程：构建源表 → 发送 → 等待发送完成 → stop
    无需 auth / register / subscribe
*/
#pragma once
#include "carrier_base.h"
#include "knt.h"

class source_ntrip : public carrier_base
{
    std::string _source_list;

public:
    source_ntrip(ConnectInfo info) : carrier_base(info)
    {
        __class__ = "source_ntrip";
    }

    ~source_ntrip() = default;

    DetachedTask run() override
    {
        // 1. 构建源表并发送
        start_bev(false, 0, true, 0);
        _source_list = CASTER::Get_Source_Table_Text();
        build_source_table();

        // 2. 事件循环 — 等待发送完成 / TCP 断连
        while (auto evt = co_await _events.next())
        {
            switch (evt->type)
            {
            case CarrierEventType::BevWrite:
            {
                // 检查发送缓冲区是否已清空
                auto length = evbuffer_get_length(_send_evbuf);
                if (length == 0)
                {
                    auto unsend = evbuffer_get_length(bufferevent_get_output(_bev));
                    if (unsend == 0)
                    {
                        spdlog::info("[{}]: Send SourceTable Finished, user [{}], addr:[{}:{}]",
                                     __class__, _info.user_name(), _info.addr(), _info.port());
                        stop();
                        co_return;
                    }
                }
                else
                {
                    bufferevent_write_buffer(_bev, _send_evbuf);
                }
                break;
            }

            case CarrierEventType::BevEvent:
                spdlog::info("[{}]: disconnected, addr:[{}:{}]", __class__, _info.addr(), _info.port());
                stop();
                co_return;

            default:
                break;
            }
        }

        co_return;
    }

private:
    int build_source_table()
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
};
