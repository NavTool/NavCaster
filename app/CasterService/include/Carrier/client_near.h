/*
    client_near.h — 协程版本的就近接入 Carrier（混合设计）
    初始化：auth_login → caster_register(NEAREST)（顺序 co_await）
    running：co_await _events.next() 事件循环（读取NMEA解析坐标 / 订阅数据转发 / 踢下线 / 断连）
*/
#pragma once
#include "carrier_base.h"
#include "decode_nmea.h"

class client_near : public carrier_base
{
public:
    client_near(ConnectInfo info) : carrier_base(info)
    {
        __class__ = "client_near";
    }

    ~client_near() = default;

    int stop() override
    {
        stop_bev();
        auth_logout();
        unsubscribe();
        caster_withdraw();

        _info.set_operate(OPERATE_TYPE_DESTORY);
        QUEUE::Push(_info);

        spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
        return 0;
    }

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

        // 2. 注册（NEAREST 模式：不立即订阅，等 NMEA 数据触发）
        auto reg = co_await co_caster_register(CasterRegisterType::NEAREST);
        if (reg.type != CasterReply::OK)
        {
            spdlog::warn("[{}]: caster register failed, addr:[{}:{}]", __class__, _info.addr(), _info.port());
            stop();
            co_return;
        }

        // 3. 注册成功，进入 running 状态
        start_bev(true, 0, false, 0);
        start_timeout_event(5);
        auto reply_str = build_nrtip_reply(CONNECT_TYPE_CLIENT, _ntrip_version2, _transfer_with_chunked);
        send_data(reply_str.c_str(), reply_str.size(), false);
        spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());

        // 4. 统一事件循环 — 解析 NMEA / 订阅数据 / 踢下线 / 断连
        while (auto evt = co_await _events.next())
        {
            switch (evt->type)
            {
            case CarrierEventType::BevRead:
            {
                // 接收客户端上传的 NMEA 数据
                auto data = read_data(false);
                // TODO: 解析 NMEA 坐标，判断位移是否 > 1km，触发 CASTER::Sub_Near_Raw_Data 订阅最近基站
                break;
            }

            case CarrierEventType::SubscribeReply:
                if (evt->caster_type == CasterReply::STRING)
                {
                    send_data(reinterpret_cast<const char *>(evt->data.data()), evt->data.size(), _transfer_with_chunked);
                }
                else if (evt->caster_type == CasterReply::OK)
                {
                    // 订阅成功确认
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

            default:
                break;
            }
        }

        co_return;
    }
};
