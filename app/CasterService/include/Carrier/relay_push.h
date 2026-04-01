#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "carrier_base.h"

#include <spdlog/spdlog.h>

// 向第三方推送本地频道的数据

class relay_push : public carrier_base
{
public:
    relay_push(ConnectInfo info);
    ~relay_push();

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
