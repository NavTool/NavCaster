#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "carrier_base.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class source_ntrip : public carrier_base
{
private:
    // 内部成员变量
    std::string _source_list;

public:
    source_ntrip(ConnectInfo info);
    ~source_ntrip();

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

private:
    int build_source_table();
};
