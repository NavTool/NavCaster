#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "decode_rtcm.h"
#include "carrier_base.h"

#include <spdlog/spdlog.h>

// 从第三方拉取数据，推送到本地的频道

class relay_pull : public carrier_base
{
private:
    decode_rtcm _str_decoder;

public:
    relay_pull(ConnectInfo info);
    ~relay_pull();

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
