/*
    relay_pull_co.h — 协程版本的 relay 拉取 Carrier（混合设计）
    初始化：create_bev → 等待连接 → 发送请求 → 等待握手 → caster_register（顺序 co_await）
    running：co_await _events.next() 事件循环（读取数据→publish / 踢下线 / 断连→重连）
*/
#pragma once
#include "carrier_coroutine.h"
#include "decode_rtcm.h"

class relay_pull_co : public carrier_coroutine
{
    decode_rtcm _str_decoder;

public:
    relay_pull_co(ConnectInfo info) : carrier_coroutine(info)
    {
        __class__ = "relay_pull_co";
    }

    ~relay_pull_co() = default;

    DetachedTask run() override
    {
        while (true)
        {
            // 1. 创建 bufferevent 并发起 TCP 连接
            _connect_key = create_bev(_info.addr(), _info.port());
            start_bev(true, 0, false, 0);

            // 2. 等待连接建立
            auto events = co_await co_wait_bev_event();
            if (!(events & BEV_EVENT_CONNECTED))
            {
                spdlog::warn("[{}]: connect failed, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                destory_bev(_connect_key);
                co_await co_sleep(5);
                continue;
            }

            // 3. 发送 NTRIP 拉取请求
            auto req = build_ntrip_request(ConnectType::CONNECT_TYPE_PULL,
                                           _ntrip_version2,
                                           _info.mount_point(),
                                           _info.http_host(),
                                           _info.ntrip_auth());
            send_data(req.c_str(), req.size(), false);

            // 4. 等待握手响应
            auto resp_data = co_await co_wait_bev_read();
            if (resp_data.empty())
            {
                spdlog::warn("[{}]: handshake failed (no data), mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                destory_bev(_connect_key);
                co_await co_sleep(5);
                continue;
            }

            bool v2 = false, chunked = false;
            bool ok = verify_ntrip_response(reinterpret_cast<const char *>(resp_data.data()), resp_data.size(), v2, chunked);
            if (!ok)
            {
                spdlog::warn("[{}]: handshake verify failed, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                destory_bev(_connect_key);
                co_await co_sleep(5);
                continue;
            }
            _ntrip_version2 = v2;
            _transfer_with_chunked = chunked;

            // 5. 注册到 caster
            auto reg = co_await co_caster_register(CasterRegisterType::PULL);
            if (reg.type != CasterReply::OK)
            {
                spdlog::warn("[{}]: caster register failed, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
                destory_bev(_connect_key);
                co_await co_sleep(5);
                continue;
            }

            // 6. 进入 running 状态
            start_bev(true, 0, false, 0);
            spdlog::info("[{}]: running, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());

            // 7. 统一事件循环 — 读取数据 / 踢下线 / 断连
            bool disconnected = false;
            while (auto evt = co_await _events.next())
            {
                switch (evt->type)
                {
                case CarrierEventType::BevRead:
                    publish_data(reinterpret_cast<const char *>(evt->data.data()), evt->data.size());
                    break;

                case CarrierEventType::RegisterReply:
                    if (evt->caster_type == CasterReply::ERR)
                    {
                        spdlog::warn("[{}]: caster kicked, mount [{}]", __class__, _info.mount_point());
                        disconnected = true;
                    }
                    break;

                case CarrierEventType::BevDisconnect:
                    disconnected = true;
                    break;

                default:
                    break;
                }
                if (disconnected)
                    break;
            }

            // 重连准备
            spdlog::info("[{}]: disconnected, will retry, mount [{}], addr:[{}:{}]", __class__, _info.mount_point(), _info.addr(), _info.port());
            stop_bev();
            caster_withdraw();
            destory_bev(_connect_key);
            _events.reset();
            co_await co_sleep(5);
        }
    }
};
