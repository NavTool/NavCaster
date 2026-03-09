

#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

// 这个本质应当是内部封装了基本的bev连接和操作函数以及基本的Auth和Caster交互逻辑，提供一些虚函数让不同类型的carrier去重写实现不同的功能

class carrier_base
{

    // 对于每一个 carrier

    /*
        共有的对象：
        一个bufferevent

        一些连接的参数
            // 用户名 密码
            // 挂载点名称等


        基本的函数

            用户管理相关
                注册到Auth
                Auth的回调函数（成功/失败）
                从Auth取消注册

            数据传输相关
                注册到Caster
                Caster的回调函数（成功/失败/通知下线）
                从Caster取消注册

                向Caster发布数据
                从Caster订阅数据

            管理函数

                bev的事件处理函数（默认函数+重载函数
                定时函数（默认函数+重载函数）

    */

public:
    // 需要实现的虚函数
    void process_recv_data(const char *data, size_t length); // 处理接收数据的函数， 这个函数的实现会根据不同的类型有不同的处理逻辑，所以是虚函数
    void process_send_data(const char *data, size_t length); // 处理发送数据的函数， 这个函数的实现会根据不同的类型有不同的处理逻辑，所以是虚函数
    void process_event(int type);                            // 处理事件的函数， 这个函数的实现会根据不同的类型有不同的处理逻辑，所以是虚函数
    void process_timeout();                                  // 处理定时器超时的函数， 这个函数的实现会根据不同的类型有不同的处理逻辑，所以是虚函数

public:
    carrier_base();
    ~carrier_base();

    virtual int start(); // 启动函数，注册Auth和Caster的回调函数，初始化bev连接等
    virtual int stop();  // 停止函数，取消注册Auth和Caster的回调函数，释放bev连接等
    virtual int update();
    virtual int pause();
    virtual int unpause();
    
    virtual int running();
    virtual int retry();

    // init bev连接(外部传入的Bev)
    int init_bev(bufferevent *bev);
    // 创建Bev连接
    int create_bev(std::string ip, int port); // 创建bev连接，并连接到指定ip和端
    // 设置Bev事件
    int set_bev(bool enable_read, time_t read_timeout_ms, bool enable_write, time_t write_timeout_ms, bool enable_event); // 设置BEV事件的启动状态  读写事件定时器
    // 释放Bev连接
    int free_bev(); // 释放bev连接

    // 启动定时器函数
    int set_timeout(time_t time_ms);

public:
    // 请求和回复的函数
    int bev_send_reply(ConnectType type, bool version2, bool chuncked);                                         //  有些类型需要 有些类型不需要
    int bev_send_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth); // 虚函数  有些类型需要 有些类型不需要

    int bev_send_data(const char *data, size_t length); // 发送数据的函数， 这个函数的实现会根据不同的类型有不同的处理逻辑，所以是虚函数

    int update_tcp_delay_info();

protected:
    bufferevent *_bev;

    // bev的超时定时器
    timeval _bev_timeout_tv; // bev读超时定时器

    evbuffer *_send_evbuf; // 发送缓冲区
    evbuffer *_recv_evbuf; // 接收缓冲区

    // 定时器和定时事件标识
    bool _timeout_ev_flag = false; // 是否将timeout_ev注册到event_base的标记
    event *_timeout_ev;
    timeval _timeout_tv;

public:
    // 重复逻辑

    static void Auth_Login_Callback(const char *request, void *arg, auth_reply *reply);
    static void Caster_Register_Callback(const char *request, void *arg, catser_reply *reply);
    static void Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply);

    virtual int publish_recv_raw_data() = 0;

    virtual int transfer_sub_raw_data(const char *data, size_t length) = 0;

    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);
};
