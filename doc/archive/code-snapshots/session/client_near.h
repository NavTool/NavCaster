// Archived documentation snapshot. Not current product source; see doc/archive/code-snapshots/README.md.
/*
    client_near.h — 协程版本的就近接入 Carrier（混合设计）
    初始化：auth_login → caster_register(NEAREST) → subscribe_near（顺序 co_await）
    running：co_await _events.next() 事件循环（上传GGA→Core解析并切换 / 订阅数据转发 / 踢下线 / 断连）
    数据解析由CasterCore统一管理：Core解析GGA坐标，执行GEORADIUS查询，自动切换最近基站订阅
*/
#pragma once
#include "carrier_base.h"

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

        _info.set_operate(OPERATE_TYPE_DESTROY);
        QUEUE::Push(_info);

        spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
        return 0;
    }

    DetachedTask run() override
    {
        auto self = shared_from_this(); // 保持 carrier 存活直到协程退出

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

        // 3. 使用 Listener 已解析的 GGA 经纬度进行初始订阅
        double init_lat = _info.ntrip_lat();
        double init_lon = _info.ntrip_lon();

        if (init_lat != 0 || init_lon != 0)
        {
            spdlog::info("[{}]: initial GGA position: lat={:.6f}, lon={:.6f}, mount [{}]",
                         __class__, init_lat, init_lon, _info.mount_point());
            subscribe(init_lon, init_lat);
        }
        else
        {
            // 无初始位置，空订阅等待 Core 后续从上传数据中解析 GGA 并自动更新
            subscribe(0, 0);
        }

        // 如果 buffer 中有残余数据，在启动 bev 读取之前发送到 Core 作为第一条数据
        if (_bev)
        {
            evbuffer *input = bufferevent_get_input(_bev);
            size_t trailing_len = evbuffer_get_length(input);
            if (trailing_len > 0)
            {
                std::vector<char> buf(trailing_len);
                evbuffer_remove(input, buf.data(), trailing_len);
                publish_data(buf.data(), trailing_len);
            }
        }

        // 4. 进入 running 状态
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
                // 接收客户端上传的NMEA数据，发布到Core（Core负责解析GGA并管理最近基站切换）
                auto data = read_data(false);
                publish_data(reinterpret_cast<const char *>(data.data()), data.size());
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
