#pragma once

#include "ntrip_global.h"
#include "ntrip_config.h"
#include "Connector/ntrip_listener.h"
// #include "Connector/ntrip_relay_connector.h"
#include "Carrier/client_ntrip.h"
#include "Carrier/server_ntrip.h"
// #include "Carrier/server_relay.h"
#include "Carrier/source_ntrip.h"
#include "Carrier/client_near.h"
#include "Carrier/relay_pull.h"
#include "Carrier/relay_push.h"

// #include "../extra/heart_beat/heart_beat.h"
#include "../extra/license_check/license_check.h"

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
    int compontent_init();
    int compontent_stop();

    int extra_init();
    int extra_stop();

private:
    int request_process(ConnectInfo req);

    int operate_client_ntrip(ConnectInfo req);
    int operate_server_ntrip(ConnectInfo req);
    int operate_source_ntrip(ConnectInfo req);
    int operate_client_near(ConnectInfo req);
    int operate_client_proxy(ConnectInfo req);
    int operate_client_alias(ConnectInfo req);

    // // 任务处理函数
    // int request_process(json req);

    // int create_client_ntrip(std::shared_ptr<ReqBase> req); // 用Ntrip协议登录的用户(一个挂载点一个)
    // int close_client_ntrip(std::shared_ptr<ReqBase> req);

    // int create_server_ntrip(std::shared_ptr<ReqBase> req); // 基站主动接入产生的数据源
    // int close_server_ntrip(std::shared_ptr<ReqBase> req);

    // int create_source_ntrip(std::shared_ptr<ReqBase> req); // 用Ntrip协议获取源列表
    // int close_source_ntrip(std::shared_ptr<ReqBase> req);  // 用Ntrip协议获取源列表

    // int create_client_near(std::shared_ptr<ReqBase> req); // 用Ntrip协议登录的用户(最近挂载点)
    // int close_client_near(std::shared_ptr<ReqBase> req);

    // int create_relay_pull(std::shared_ptr<ReqBase> req); // 用Ntrip协议登录的用户(最近挂载点)
    // int stop_relay_pull(std::shared_ptr<ReqBase> req);   // 用Ntrip协议登录的用户(最近挂载点)
    // int update_relay_pull(std::shared_ptr<ReqBase> req);
    // int close_relay_pull(std::shared_ptr<ReqBase> req);

    // int create_relay_push(std::shared_ptr<ReqBase> req); // 用Ntrip协议登录的用户(最近挂载点)
    // int stop_relay_push(std::shared_ptr<ReqBase> req);   // 用Ntrip协议登录的用户(最近挂载点)
    // int update_relay_push(std::shared_ptr<ReqBase> req);
    // int close_relay_push(std::shared_ptr<ReqBase> req);

    // 请求处理失败，关闭连接
    int close_unsuccess_req_connect(json req);

private:
    // 连接器
    // ntrip_compat_listener *_compat_listener; // 被动接收Ntrip连接
    // ntrip_relay_connector *_relay_connetcotr; // 主动创建Ntrip连接

    // 连接-对象索引
public:
    std::unordered_map<std::string, bufferevent *> _connect_map; // Connect_Key,bev
    std::unordered_map<std::string, ConnectInfo> _connect_info_map;

private:
    std::unordered_map<std::string, std::shared_ptr<server_ntrip>> _server_map; // Connect_Key,server_ntrip
    std::unordered_map<std::string, std::shared_ptr<client_ntrip>> _client_map; // Connect_Key,client_ntrip
    std::unordered_map<std::string, std::shared_ptr<source_ntrip>> _source_map; // Connect_Key,source_ntrip

    std::unordered_map<std::string, std::shared_ptr<client_near>> _near_map; // Connect_Key,client_near
    std::unordered_map<std::string, std::shared_ptr<relay_pull>> _pull_map;  // Connect_Key,relay_pull
    std::unordered_map<std::string, std::shared_ptr<relay_push>> _push_map;  // Connect_Key,relay_pull

private:
    event_base *_base;
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

private:
    // 扩展模块 许可检查功能--------------------------------------------------------------------------
    event *_license_check_ev;
    timeval _license_check_tv;

    license_check _license_check;

    int init_license_check();                                                        // 初始化许可检查
    static void License_Check_Callback(evutil_socket_t fd, short events, void *arg); // 许可检查的函数

private:
    // 扩展模块，Relay请求处理

    static void Relay_Request_Callback(void *arg, CasterBroadcastType type, std::string req_str);

    // private:
    //     // 扩展模块 心跳上传功能--------------------------------------------------------------------------
    //     event *_heart_beat_ev;
    //     timeval _heart_beat_tv;

    //     heart_beat _heart_beat;

    //     int init_heart_beat();                                                        // 初始化信息上传功能
    //     static void Heart_Beat_Callback(evutil_socket_t fd, short events, void *arg); // 定期上传信息的回调
};
