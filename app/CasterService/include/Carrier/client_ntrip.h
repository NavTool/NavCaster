/*
    client_ntrip.h — 协程版本的移动站 Carrier（混合设计）
    初始化：auth_login → caster_register → subscribe（顺序 co_await）
    running：co_await _events.next() 事件循环（订阅数据 + 踢下线 + 断连）
*/
#pragma once
#include "carrier_base.h"
#include "decode_nmea.h"

class client_ntrip : public carrier_base
{
public:
    client_ntrip(ConnectInfo info) : carrier_base(info)
    {
        __class__ = "client_ntrip";
    }

    ~client_ntrip() = default;

    DetachedTask run() override
    {
        // 1. 认证
        auto auth = co_await co_auth_login(AuthType::CLIENT);
        if (auth.type != AuthReply::OK)
        {
            spdlog::warn("[{}]: auth login failed, addr:[{}:{}]", __class__, _info.addr(), _info.port());
            stop();
            co_return;
        }

        // 2. 注册
        auto reg = co_await co_caster_register(CasterRegisterType::CLIENT);
        if (reg.type != CasterReply::OK)
        {
            spdlog::warn("[{}]: caster register failed, addr:[{}:{}]", __class__, _info.addr(), _info.port());
            stop();
            co_return;
        }

        // 3. 订阅
        auto sub = co_await co_caster_subscribe();
        if (sub.type != CasterReply::OK)
        {
            spdlog::warn("[{}]: subscribe failed, addr:[{}:{}]", __class__, _info.addr(), _info.port());
            stop();
            co_return;
        }

        // 4. 订阅成功，进入 running 状态
        start_bev(true, 0, false, 0);
        start_timeout_event(5);
        auto reply_str = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);
        send_data(reply_str.c_str(), reply_str.size(), false);
        spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());

        // 5. 统一事件循环 — 消费订阅数据 / 响应踢下线 / TCP 断连
        while (auto evt = co_await _events.next())
        {
            switch (evt->type)
            {
            case CarrierEventType::SubscribeReply:
                if (evt->caster_type == CasterReply::STRING)
                {
                    send_data(reinterpret_cast<const char *>(evt->data.data()), evt->data.size(), _transfer_with_chunked);
                }
                else if (evt->caster_type == CasterReply::ERR)
                {
                    spdlog::warn("[{}]: subscribe kicked, addr:[{}:{}]", __class__, _info.addr(), _info.port());
                    stop();
                    co_return;
                }
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

            case CarrierEventType::BevEvent:
                spdlog::info("[{}]: disconnected, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                stop();
                co_return;

            case CarrierEventType::BevRead:

                break;

            case CarrierEventType::Timeout:
                // 发送心跳或其他定时任务
                break;

            default:
                break;
            }
        }

        co_return;
    }
};
