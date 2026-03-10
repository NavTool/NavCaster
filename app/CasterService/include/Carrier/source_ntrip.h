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

    int write_cb(struct bufferevent *bev) override;               // bev写回调函数
    int event_cb(struct bufferevent *bev, short events) override; // bev事件回调函数

private:
    int build_source_table();
};
