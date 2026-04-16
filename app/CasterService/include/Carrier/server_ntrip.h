/*
    server_ntrip.h — 协程版本的基站 Carrier（混合设计）
    初始化：auth_login → caster_register（顺序 co_await）
    running：co_await _events.next() 事件循环（上传数据→publish / 踢下线 / 断连）
*/
#pragma once
#include "carrier_base.h"
#include "decode_rtcm.h"

class server_ntrip : public carrier_base
{
    decode_rtcm _str_decoder;

public:
    server_ntrip(ConnectInfo info) : carrier_base(info)
    {
        __class__ = "server_ntrip";
    }

    ~server_ntrip() = default;

    DetachedTask run() override
    {
        // 1. 认证
        auto auth = co_await co_auth_login(AuthType::SERVER);
        if (auth.type != AuthReply::OK)
        {
            spdlog::warn("[{}]: auth login failed, addr:[{}:{}]", __class__, _info.addr(), _info.port());
            stop();
            co_return;
        }

        // 2. 注册
        auto reg = co_await co_caster_register(CasterRegisterType::SERVER);
        if (reg.type != CasterReply::OK)
        {
            spdlog::warn("[{}]: caster register failed, addr:[{}:{}]", __class__, _info.addr(), _info.port());
            stop();
            co_return;
        }

        // 3. 注册成功，进入 running 状态
        start_bev(true, 0, false, 0);
        start_timeout_event(5);
        auto reply_str = build_nrtip_reply(CONNECT_TYPE_SERVER, _ntrip_version2, _transfer_with_chunked);
        send_data(reply_str.c_str(), reply_str.size(), false);
        spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());

        // 4. 统一事件循环 — 上传数据 / 响应踢下线 / TCP 断连
        while (auto evt = co_await _events.next())
        {
            switch (evt->type)
            {
            case CarrierEventType::BevRead:
            {
                // 基站上传数据，发布到 caster
                auto data = read_data(_transfer_with_chunked);
                publish_data(reinterpret_cast<const char *>(data.data()), data.size());

                // 解析RTCM数据流，提取坐标和报文统计
                _str_decoder.Decode(reinterpret_cast<const char *>(data.data()), data.size());
                break;
            }

            case CarrierEventType::AuthReply:
                if (evt->auth_type != AuthReply::OK)
                {
                    spdlog::warn("[{}]: auth kicked, addr:[{}:{}]", __class__, _info.addr(), _info.port());
                    stop();
                    co_return;
                }
                break;

            case CarrierEventType::RegisterReply:
                if (evt->caster_type == CasterReply::ERR)
                {
                    spdlog::warn("[{}]: caster kicked, addr:[{}:{}]", __class__, _info.addr(), _info.port());
                    stop();
                    co_return;
                }
                break;

            case CarrierEventType::BevEvent:
                spdlog::info("[{}]: disconnected, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                stop();
                co_return;

            case CarrierEventType::Timeout:
                // 定时上报解析出的源列表信息
                update_source_info();
                break;

            default:
                break;
            }
        }

        co_return;
    }

private:
    void update_source_info()
    {
        // 上报坐标信息
        if (_str_decoder._has_position)
        {
            CASTER::Set_Base_Coord_Info(
                _mount_point.c_str(), _connect_key.c_str(),
                _str_decoder._ecef_x, _str_decoder._ecef_y, _str_decoder._ecef_z);
        }

        // 上报RTCM解析出的源列表信息（报文类型、卫星系统）
        if (!_str_decoder._msg_stats.empty())
        {
            CASTER::Set_Base_Source_Info(
                _mount_point.c_str(), _connect_key.c_str(),
                _str_decoder.get_format_details(), _str_decoder.get_nav_system());
        }
    }
};
