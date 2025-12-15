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

class relay_pull
{
private:
    // 连接相关参数和上下文
    json _info;
    std::string _login_mpt; // 登录参数
    int _type = 0;
    std::string _target_ip;
    int _target_port = 0;
    std::string _target_mpt;
    std::string _target_account;
    std::string _target_password;

    int _connect_timeout = 0;

    event_base *_base = nullptr;

private:
    // 内部成员 和内部维护变量
    std::string _connect_key;
    std::string _user_name = "SYSTEM";
    std::string _ip; // 用户IP
    int _port;       // 用户端口
    bool _NtripVersion2 = false;
    bool _transfer_with_chunked = false;
    size_t _chunked_size = 0;

    timeval _connect_timeout_tv;  // 连接超时时间
    timeval _bev_read_timeout_tv; // bufferevent读超时时间
    bufferevent *_bev = nullptr;

    evbuffer *_send_evbuf;
    evbuffer *_recv_evbuf;

    bool _timeout_ev_flag = false; // 是否将timeout_ev注册到event_base的标记
    timeval _timeout_tv;           // 定时时间间隔
    event *_timeout_ev;            // 定时事件

    timeval _reconnect_tv; // 重连时间间隔
    event *_reconnect_ev;  // 重连定时事件

    decode_rtcm _str_decoder;

public:
    relay_pull(json req, event_base *base);
    ~relay_pull();

    int start(); // 启动连接
    int stop();  // 停止连接

private:
    int runing(); // 连接成功，进入运行状态
    int retry();  // 重试连接

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

    static void Caster_Register_Callback(const char *request, void *arg, catser_reply *reply);
};
