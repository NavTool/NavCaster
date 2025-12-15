#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class source_ntrip
{
private:
    // 基本上下文 在构造函数的时候传入
    json _info;                  // 原始请求
    std::string _connect_key;    // 连接唯一标识
    std::string _user_name;      // 用户名
    std::string _ip;             // 用户IP
    int _port;                   // 用户端口
    bool _NtripVersion2 = false; // 这个决定回复的消息是按照1.0还是2.0

    bufferevent *_bev;

private:
    // 内部成员变量
    std::string _source_list;

    evbuffer *_send_evbuf; // 发送缓冲区

public:
    source_ntrip(json req, bufferevent *bev);
    ~source_ntrip();

    int start();
    int stop();

    static void WriteCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);

private:
    int build_source_table();
};
