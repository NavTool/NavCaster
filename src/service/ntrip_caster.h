#pragma once

#include "ntrip_global.h"
#include "ntrip_config.h"
#include "ntrip_listener.h"
// #include "ntrip_relay_connector.h"
#include "client_ntrip.h"
#include "server_ntrip.h"
// #include "server_relay.h"
#include "source_ntrip.h"
#include "client_near.h"
#include "relay_pull.h"
#include "relay_push.h"
#include "carrier_base.h"

// #include "extra/heart_beat/heart_beat.h"
#include "license_check/license_check.h"
#include "http_handler.h"

#include <event2/util.h>
#include <event2/event.h>
#include <event2/http.h>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <queue>
#include <list>
#include <mutex>
#include <memory>
#include <thread>
#include <unordered_map>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class ntrip_caster
{
public:
    // 公开的接口
    ntrip_caster();
    ~ntrip_caster();

    static ntrip_caster *getInstance();

    int start();
    int stop();

private:
    bool _output_state;
    int _refresh_state_interval;

    // 状态数据
    json _state_info;
    int update_state_info();

    // 定期任务
    int periodic_task();

private:
    // 程序启动和停止
    int component_init();
    int component_stop();

    int extra_init();
    int extra_stop();

private:
    int process_request(ConnectInfo req);
    int process_relay(const broadcast_msg &msg);

private:
    Carrier<server_ntrip> Servers;
    Carrier<client_ntrip> Clients;
    Carrier<source_ntrip> Sources;
    Carrier<client_near> Nears;
    Carrier<relay_pull> Pulls;
    Carrier<relay_push> Pushs;

private:
    event_base *_base;
    event_base *_http_base = nullptr;  // HTTP/SSE 独立事件循环
    std::thread _http_thread;          // HTTP 工作线程
    // process处理事件
    event *_process_event;
    // 定时器和定时事件
    event *_timeout_ev;
    timeval _timeout_tv;

public:
    int start_server_thread();
    static void *event_base_thread(void *arg);

    // libevent回调
    static void Request_Process_Cb(evutil_socket_t fd, short what, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);
    static void Relay_Request_Callback(void *arg, const broadcast_msg &msg);

private:
    // // 扩展模块 许可检查功能--------------------------------------------------------------------------
    // event *_license_check_ev;
    // timeval _license_check_tv;

    // license_check _license_check;

    // int init_license_check();                                                        // 初始化许可检查
    // static void License_Check_Callback(evutil_socket_t fd, short events, void *arg); // 许可检查的函数

private:
    // 扩展模块，Relay请求处理

private:
    // 扩展模块，HTTP API
    http_handler _http_handler;
    redis_adapter _http_caster_redis;
    redis_adapter _http_auth_redis;

    // private:
    //     // 扩展模块 心跳上传功能--------------------------------------------------------------------------
    //     event *_heart_beat_ev;
    //     timeval _heart_beat_tv;

    //     heart_beat _heart_beat;

    //     int init_heart_beat();                                                        // 初始化信息上传功能
    //     static void Heart_Beat_Callback(evutil_socket_t fd, short events, void *arg); // 定期上传信息的回调
};
