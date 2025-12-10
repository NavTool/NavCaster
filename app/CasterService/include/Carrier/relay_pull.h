#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "decode_rtcm.h"
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 从第三方拉取数据，推送到本地的频道

class relay_pull_item
{
private:
    /* data */
    event_base *_base = nullptr;
    bufferevent *_bev = nullptr;

    timeval _connect_timeout_tv;

public:
    relay_pull_item(json req, event_base *base);
    ~relay_pull_item();

    int start();
    int stop();

    int retry();

private:
    // 连接相关参数和上下文
    json _info;
    // 登录参数
    std::string _login_mpt;
    int _type = 0;
    std::string _target_ip;
    int _target_port = 0;
    std::string _target_mpt;
    std::string _target_account;
    std::string _target_password;

private:
    static void ConnectedCallback(struct bufferevent *bev, short events, void *arg);
    static void VerifyCallback(struct bufferevent *bev, void *arg);

    // 发送验证消息
    int send_login_request();

    // 验证登录响应
    int verify_login_response();

    // 建立连接，开始推送
    int request_new_relay_server();

private:
    int _connect_timeout = 0;
    timeval _bev_read_timeout_tv;
    // 数据传输相关上下文

    std::string _connect_key;
    std::string _mount_point;

    bool _NtripVersion2 = false;
    bool _transfer_with_chunked = false;
    size_t _chunked_size = 0;

    evbuffer *_send_evbuf;
    evbuffer *_recv_evbuf;

    timeval _timeout_tv;
    event *_timeout_ev;
    bool _timeout_ev_flag = false; // 是否将timeout_ev注册到event_base的标记

    decode_rtcm _str_decoder;

private:
    int runing();

    int send_heart_beat_to_server();

    int publish_recv_raw_data();
    int publish_data_from_chunk();
    int publish_data_from_evbuf();

    int update_pull_status_info();

    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    static void Caster_Register_Callback(const char *request, void *arg, catser_reply *reply);
};
