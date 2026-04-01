/*
    server_ntrip_co.h — 协程版本的基站 Carrier（混合设计）
    初始化：auth_login → caster_register（顺序 co_await）
    running：co_await _events.next() 事件循环（上传数据→publish / 踢下线 / 断连）
*/
#pragma once
#include "carrier_coroutine.h"
#include "decode_rtcm.h"

class server_ntrip_co : public carrier_coroutine
{
    decode_rtcm _str_decoder;

public:
    server_ntrip_co(ConnectInfo info) : carrier_coroutine(info)
    {
        __class__ = "server_ntrip_co";
    }

    ~server_ntrip_co() = default;

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
                // 基站上传数据，发布到 caster
                publish_data(reinterpret_cast<const char *>(evt->data.data()), evt->data.size());
                break;

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

            case CarrierEventType::BevDisconnect:
                spdlog::info("[{}]: disconnected, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                stop();
                co_return;

            default:
                break;
            }
        }

        co_return;
    }
};
