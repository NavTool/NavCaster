#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "decode_rtcm.h"
#include "carrier_base.h"
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class server_ntrip : public carrier_base
{
private:
    decode_rtcm _str_decoder;

public:
    server_ntrip(ConnectInfo info);
    ~server_ntrip();

    int init() override;
    int start() override;
    int stop() override;

    int read_cb(struct bufferevent *bev) override;                // bev读回调函数
    int write_cb(struct bufferevent *bev) override;               // bev写回调函数
    int event_cb(struct bufferevent *bev, short events) override; // bev事件回调函数
    int timeout_cb() override;                                    // 定时器回调函数

    int login_cb(auth_reply *reply) override;      // Auth登录回调函数
    int register_cb(catser_reply *reply) override; // Caster注册回调函数


private:
    int runing();


    int send_heart_beat_to_server();

    int publish_recv_raw_data();
    int publish_data_from_chunk();
    int publish_data_from_evbuf();

    int update_tcp_delay_info();

    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    static void Auth_Login_Callback(const char *request, void *arg, auth_reply *reply);
    static void Caster_Register_Callback(const char *request, void *arg, catser_reply *reply);

private:
};
