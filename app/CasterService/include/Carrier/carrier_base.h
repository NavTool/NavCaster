

#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

std::string build_nrtip_reply(ConnectType type, bool version2, bool chuncked);                                         //  有些类型需要 有些类型不需要
std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth); // 虚函数  有些类型需要 有些类型不需要

class carrier_base
{
public:
    ConnectInfo _info;

    bool _ntrip_version2 = false;        // 这个决定回复的消息是按照1.0还是2.0
    bool _transfer_with_chunked = false; // 这个决定数据传输是否使用chunk
    size_t _chunked_size = 0;

public:
    evbuffer *_send_evbuf; // 发送缓冲区
    evbuffer *_recv_evbuf; // 接收缓冲区

    // 定时器和定时事件标识
    bool _timeout_ev_flag = false; // 是否将timeout_ev注册到event_base的标记
    event *_timeout_ev;
    timeval _timeout_tv;

public:
    carrier_base(ConnectInfo info);
    ~carrier_base();

public:
    virtual int init() = 0;  // 初始化不同派生类所需要的资源，基类的资源在构造函数的时候就已经构建
    virtual int start() = 0; // 启动函数，注册Auth和Caster的回调函数，初始化bev连接等
    virtual int stop() = 0;  // 停止函数，取消注册Auth和Caster的回调函数，释放bev连接等
    virtual int runing() = 0;

    virtual int read_cb(struct bufferevent *bev) = 0;                // bev读回调函数
    virtual int write_cb(struct bufferevent *bev) = 0;               // bev写回调函数
    virtual int event_cb(struct bufferevent *bev, short events) = 0; // bev事件回调函数
    virtual int timeout_cb() = 0;                                    // 定时器回调函数

    virtual int login_cb(auth_reply *reply) = 0;       // Auth登录回调函数
    virtual int register_cb(catser_reply *reply) = 0;  // Caster注册回调函数
    virtual int subscribe_cb(catser_reply *reply) = 0; // 订阅回调函数

public:
    int start_bev(bool enable_read_cb, time_t read_timeout_sec, bool enable_write_cb, time_t write_timeout_sec); // 启动bev连接，注册回调函数
    int stop_bev();                                                                                              // 停止bev连接，取消注册回调函数

    int start_timeout_event(time_t timeout_sec); // 启动定时器事件
    int stop_timeout_event();                    // 停止定时器事件

    int auth_login(AuthType type);
    int auth_logout(AuthType type);

    int caster_register(CasterRegisterType type);
    int caster_withdraw(CasterRegisterType type);

    std::vector<uint8_t> read_data(bool chuncked); //  从bev读取数据

    int send_data(const char *data, size_t len);    // 向Bev发送数据
    int publish_data(const char *data, size_t len); // 发布数据到CASTER，数据来源于bev的读回调函数

    std::vector<uint8_t> read_data_from_evbuf();

    std::vector<uint8_t> read_data_from_chunk();

public:
    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void WriteCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    static void AuthLoginCallback(const char *request, void *arg, auth_reply *reply);
    static void CasterRegisterCallback(const char *request, void *arg, catser_reply *reply);
};
