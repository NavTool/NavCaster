

#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

std::string build_nrtip_reply(ConnectType type, bool version2, bool chuncked);                                         //  有些类型需要 有些类型不需要
std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth); // 虚函数  有些类型需要 有些类型不需要

bool verify_ntrip_response(const char *data, size_t len, bool &version2, bool &chuncked);

class carrier_base
{
public:
    ConnectInfo _info;

    // 运行相关变量，这些变量在某些派生类中会与ConnectInfo中的值保持一致，但在某些派生类中可能会根据实际业务进行调整
    std::string _mount_point;            // 挂载点(发布基站数据使用的频道)
    std::string _user_name;              // 用户名(发布用户数据使用的频道)
    std::string _connect_key;            // 连接的唯一标识，格式为 type:addr:port
    bool _ntrip_version2 = false;        // 这个决定回复的消息是按照1.0还是2.0
    bool _transfer_with_chunked = false; // 这个决定数据传输是否使用chunk
    size_t _chunked_size = 0;

    std::string _subscribe_channel; // 当前正在订阅的频道
private:
    CasterRegisterType _register_type = CasterRegisterType::UNKNOWN; // Caster注册类型 上下文
    AuthType _auth_type;                                             // 用户系统注册类型

public:
    std::string __class__ = "carrier_base"; // 派生类的类名，主要用于日志输出

    bufferevent *_bev;

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
    virtual int init();  // 初始化不同派生类所需要的资源，基类的资源在构造函数的时候就已经构建
    virtual int start(); // 启动函数，注册Auth和Caster的回调函数，初始化bev连接等
    virtual int stop();  // 停止函数，取消注册Auth和Caster的回调函数，释放bev连接等
    virtual int runing();

    virtual int read_cb(struct bufferevent *bev);                // bev读回调函数
    virtual int write_cb(struct bufferevent *bev);               // bev写回调函数
    virtual int event_cb(struct bufferevent *bev, short events); // bev事件回调函数
    virtual int timeout_cb();                                    // 定时器回调函数

    virtual int login_cb(auth_reply *reply);       // Auth登录回调函数
    virtual int register_cb(caster_reply *reply);  // Caster注册回调函数
    virtual int subscribe_cb(caster_reply *reply); // 订阅回调函数

public:
    std::string create_bev(std::string addr, int port);
    int destory_bev(std::string connect_key);
    int start_bev(bool enable_read_cb, time_t read_timeout_sec, bool enable_write_cb, time_t write_timeout_sec); // 启动bev连接，注册回调函数
    int stop_bev();                                                                                              // 停止bev连接，取消注册回调函数

    int start_timeout_event(time_t timeout_sec); // 启动定时器事件
    int stop_timeout_event();                    // 停止定时器事件

    int auth_login(AuthType type);
    int auth_logout();

    int caster_register(CasterRegisterType type);
    int caster_withdraw();

    std::vector<uint8_t> read_data(bool chuncked);              //  从bev读取数据
    int send_data(const char *data, size_t len, bool chuncked); // 向Bev发送数据

    int publish_data(const char *data, size_t len); // 发布数据到CASTER，数据来源于bev的读回调函数
    int subscribe();
    int subscribe(double lon, double lat);
    int unsubscribe();

private:
    std::vector<uint8_t> read_data_from_evbuf();
    std::vector<uint8_t> read_data_from_chunk();

public:
    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void WriteCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    static void AuthLoginCallback(const char *request, void *arg, auth_reply *reply);
    static void CasterRegisterCallback(const char *request, void *arg, caster_reply *reply);
    static void CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply);
};
