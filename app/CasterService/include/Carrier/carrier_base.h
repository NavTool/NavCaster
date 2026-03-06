

#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

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

protected:
    ConnectInfo _info;

    ConnectType _type;

    // 调试查看的变量
    std::string _mount_point;
    std::string _mount_para;
    std::string _connect_key;
    std::string _user_name;
    std::string _user_pwd;
    std::string _ip;
    int _port = 0;
    bool _ntrip_version2 = false;        // 这个决定回复的消息是按照1.0还是2.0
    bool _transfer_with_chunked = false; // 这个决定数据传输是否使用chunked编码，以及回复消息中是否包含Transfer-Encoding:chunked头(只有Ntrip2.0才会使用chunked编码)

    // 连接的bev
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
    carrier_base(ConnectInfo info, bufferevent *bev);
    ~carrier_base();

    virtual int start(); // 启动    // 如果还未创建Bev，那么要创建Bev  根据要不同的类型创建对应的函数
    virtual int stop();  // 停止


    // 主要流程
    //      已有bev  那么是被动建立的连接， 进入Auth流程，  Auth连接完成，发送回应    进行running

    //      没有bev  需要主动建立连接，不需要进入Auth流程，  TCP连接建立成功，发送请求  接收回应  进行running



private:
    virtual int runing(); // 运行

    virtual int retry();  // 重试连接  清理连接 创建定时器 重新执行start

public:
    // 需要重写的纯虚函数
    virtual int timeout() = 0;       // 定期超时函数
    virtual int event(int type) = 0; // 事件函数

public:
    // 重复逻辑

    static void Auth_Login_Callback(const char *request, void *arg, auth_reply *reply);
    static void Caster_Register_Callback(const char *request, void *arg, catser_reply *reply);
    static void Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply);

    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    virtual int publish_recv_raw_data() = 0;

    virtual int transfer_sub_raw_data(const char *data, size_t length) = 0;

private:
    int update_tcp_delay_info();

public:
    int bev_send_reply();  // 虚函数  有些类型需要 有些类型不需要

    int bev_send_request(std::string mpt,std::string host,std::string auth);  // 虚函数  有些类型需要 有些类型不需要
};
