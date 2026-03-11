#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "decode_rtcm.h"
#include "carrier_base.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 从第三方拉取数据，推送到本地的频道

class relay_pull : public carrier_base
{
private:
    bool _connected = false; // 是否已经连接到第三方服务器
    bool _reconnect = false; // 是否需要重连

    decode_rtcm _str_decoder;

public:
    relay_pull(ConnectInfo info);
    ~relay_pull();

    int init() override;
    int start() override;
    int stop() override;

    int runing() override;

    int read_cb(struct bufferevent *bev) override;                // bev读回调函数
    int write_cb(struct bufferevent *bev) override;               // bev写回调函数
    int event_cb(struct bufferevent *bev, short events) override; // bev事件回调函数
    int timeout_cb() override;                                    // 定时器回调函数

    int login_cb(auth_reply *reply) override;      // Auth登录回调函数
    int register_cb(caster_reply *reply) override; // Caster注册回调函数

private:
    int retry(); // 重试连接

    int send_login_request();       // 发送验证消息
    int verify_login_response();    // 验证登录响应
    int request_new_relay_server(); // 建立连接，开始推送

    static void ConnectedCallback(struct bufferevent *bev, short events, void *arg); // 连接建立回调
    static void VerifyCallback(struct bufferevent *bev, void *arg);                  // 连接回复信息回调
    static void ReconnectCallback(evutil_socket_t fd, short events, void *arg);      // 重连回调

private:
    int send_heart_beat_to_server();

    int publish_recv_raw_data();
    int publish_data_from_chunk();
    int publish_data_from_evbuf();

    int update_pull_status_info();

    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    static void Caster_Register_Callback(const char *request, void *arg, caster_reply *reply);
};
