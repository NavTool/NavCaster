#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "decode_rtcm.h"
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class server_ntrip
{
private:
    // 基本上下文 在构造函数的时候传入
    json _info;                          // 原始请求
    std::string _connect_key;            // 连接唯一标识
    std::string _login_mpt;              // 登录的挂载点
    std::string _user_name;              // 用户名
    std::string _ip;                     // 用户IP
    int _port;                           // 用户端口
    bool _NtripVersion2 = false;         // 这个决定回复的消息是按照1.0还是2.0
    bool _transfer_with_chunked = false; // 这个决定数据传输是否使用chunked编码，以及回复消息中是否包含Transfer-Encoding:chunked头(只有Ntrip2.0才会使用chunked编码)

    json _conf; // 配置参数
    int _connect_timeout = 0;
    int _heart_beat_interval = 0;
    std::string _heart_beat_msg;
    int _unsend_byte_limit = 0;

    bufferevent *_bev;

private:
    // 内部成员变量
    time_t _last_heart_beat_time = 0;
    size_t _chunked_size = 0;

    timeval _bev_read_timeout_tv; // bev读超时定时器

    evbuffer *_send_evbuf; // 发送缓冲区
    evbuffer *_recv_evbuf; // 接收缓冲区

    // 定时器和定时事件标识
    bool _timeout_ev_flag = false; // 是否将timeout_ev注册到event_base的标记
    event *_timeout_ev;
    timeval _timeout_tv;

    decode_rtcm _str_decoder;

public:
    server_ntrip(json req, bufferevent *bev);
    ~server_ntrip();

    int start(); // 绑定回调，然后去AUTH添加登录记录（是否允许多用户登录由auth判断并处理），如果添加成功，那就发送reply给用户，然后通知CASTER上线，如果不成功，就进入关闭流程
    int stop();

private:
    int runing();

    int bev_send_reply();
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
