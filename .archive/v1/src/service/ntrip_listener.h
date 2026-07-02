/*
    // 兼容性listener，主要是需要支持Ntrip1.0协议部分不符合HTTP协议的部分
    // 基于nginx转发设置，可实现1.0版本和2.0版本同步使用


    jison格式

    req_type
    connect_key
    mount_point
    mount_para
    mount_group
    mount_info          STR STR: ;;;0;;;;;;0;0;;;N;N;
    user_name           Authorization
    user_pwd            Authorization
    user_baseID         Authorization
    user_agent          User-Agent
    ntrip_version       Ntrip-Version
    ntrip_gga           Ntrip-GGA
    http_chunked        Transfer-Encoding
    http_host           Host


*/
#pragma once

#include "ntrip_global.h"
#include "process_queue.h"

#include "knt.h"
#include "base64.h"

#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <string>
#include <memory>
#include <unordered_map>
#include <set>

#include <regex>

class ntrip_listener
{
private:
    // 配置
    ListenerOpt _opt;

    // 内部
    bool _disable_new_connect = false;

private:
    event_base *_base;
    evconnlistener *_listener;

public:
    ntrip_listener();
    ~ntrip_listener();

    static ntrip_listener *getInstance();

    int init(ListenerOpt opt, event_base *base);

    int start();
    int stop();

    int disable_accept_new_connect(); // 停止接收新的连接
    int enable_accept_new_connect();  // 启动接收新的连接

public:
    int process_accept_request(evutil_socket_t fd);
    int process_accept_error(evconnlistener *listener);
    int process_bev_request(bufferevent *bev, std::string connect_key);
    int process_bev_event(bufferevent *bev, short events, std::string connect_key);

    int create_request(auth_reply *reply, ConnectInfo req);

    // 解析请求相关（在解析完请求后，向AUTH验证用户名密码是否合法，只要合法就允许进入下一步（不判断是否已经登录，是否是重复登录，由后续步骤进行检查））
    int Process_GET_Request(bufferevent *bev, std::string connect_key, const char *path);
    int Process_POST_Request(bufferevent *bev, std::string connect_key, const char *path);
    int Process_SOURCE_Request(bufferevent *bev, std::string connect_key, const char *path, const char *secret);
    int Process_Unsupport_Request(bufferevent *bev, std::string connect_key);

private:
    // 内部函数
    // std::string get_conncet_key(bufferevent *bev);
    ConnectInfo decode_bufferevent_req(bufferevent *bev, std::string connect_key, std::string url,std::string proxy_prorocol="");
    std::string extract_path(std::string path);
    std::string extract_para(std::string path);
    std::string decode_basic_authentication(std::string authentication);

    bool check_mount_is_valid(const std::string &str);

public:
    // 新建连接相关
    static void AcceptCallback(evconnlistener *listener, evutil_socket_t fd, sockaddr *address, int socklen, void *arg);
    static void AcceptErrorCallback(struct evconnlistener *listener, void *ctx);
    static void BevReadCallback(bufferevent *bev, void *arg);
    static void BevEventCallback(bufferevent *bev, short what, void *arg);

    // Auth验证回调
    static void Auth_Verify_Cb(const char *request, void *arg, auth_reply *reply);
};
