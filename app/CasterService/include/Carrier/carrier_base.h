

#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

std::string build_nrtip_reply(ConnectType type, bool version2, bool chuncked);
std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth);

bool verify_ntrip_response(const char *data, size_t len, bool &version2, bool &chuncked);

class carrier_base
{
public:
    ConnectInfo _info;

    // 运行相关变量
    std::string _mount_point;            // 挂载点
    std::string _user_name;              // 用户名
    std::string _connect_key;            // 连接唯一标识，格式为 type:addr:port
    bool _ntrip_version2 = false;        // 回复消息按1.0还是2.0
    bool _transfer_with_chunked = false; // 数据传输是否使用chunk
    size_t _chunked_size = 0;

    std::string _subscribe_channel; // 当前正在订阅的频道

    // relay运行状态
    bool _connected = false; // 是否已连接到第三方服务器
    bool _reconnect = false; // 是否需要重连

protected:
    CasterRegisterType _register_type = CasterRegisterType::UNKNOWN; // 运行时注册类型状态
    AuthType _auth_type = AuthType::UNKNOWN;                         // 运行时认证类型状态

public:
    std::string __class__ = "carrier_base";

    bufferevent *_bev;

    evbuffer *_send_evbuf; // 发送缓冲区
    evbuffer *_recv_evbuf; // 接收缓冲区

    bool _timeout_ev_flag = false;
    event *_timeout_ev;
    timeval _timeout_tv;

public:
    carrier_base(ConnectInfo info);
    virtual ~carrier_base();

    // ============ 生命周期（由每个派生类各自实现完整流程）============
    virtual int init() = 0;
    virtual int start() = 0;
    virtual int stop() = 0;

    // ============ 回调（由每个派生类各自实现）============
    virtual int read_cb(struct bufferevent *bev) = 0;
    virtual int write_cb(struct bufferevent *bev) = 0;
    virtual int event_cb(struct bufferevent *bev, short events) = 0;
    virtual int timeout_cb() = 0;

    virtual int login_cb(auth_reply *reply) = 0;
    virtual int register_cb(caster_reply *reply) = 0;
    virtual int subscribe_cb(caster_reply *reply) = 0;

    // ============ 工具方法（通用操作，供派生类调用）============
public:
    std::string create_bev(std::string addr, int port);
    int destory_bev(std::string connect_key);
    int start_bev(bool enable_read_cb, time_t read_timeout_sec, bool enable_write_cb, time_t write_timeout_sec);
    int stop_bev();

    int start_timeout_event(time_t timeout_sec);
    int stop_timeout_event();

    int auth_login(AuthType type);
    int auth_logout();

    int caster_register(CasterRegisterType type);
    int caster_withdraw();

    std::vector<uint8_t> read_data(bool chuncked);
    int send_data(const char *data, size_t len, bool chuncked);

    int publish_data(const char *data, size_t len);
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
