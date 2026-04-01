/*
    用户已经上线的情况下，向redis写入用户登录信息
    判断当前已登录的用户数量，如果超过限制，启动下线流程
*/
#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "decode_nmea.h"
#include "carrier_base.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class client_ntrip : public carrier_base
{
    decode_nmea _str_decoder;

public:
    client_ntrip(ConnectInfo info);
    ~client_ntrip();

    int init() override;
    int start() override;
    int stop() override;

    int read_cb(struct bufferevent *bev) override;
    int write_cb(struct bufferevent *bev) override;
    int event_cb(struct bufferevent *bev, short events) override;
    int timeout_cb() override;

    int login_cb(auth_reply *reply) override;
    int register_cb(caster_reply *reply) override;
    int subscribe_cb(caster_reply *reply) override;
};
