/*
    source_ntrip.h - non-coroutine sourcetable session.
*/
#pragma once

#include "ntrip_msg.h"

#include <string>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

class source_ntrip
{
private:
    //  固定信息
    std::string __class__ = "source_ntrip";

private:
    // 传递信息
    ConnectInfo _info;

private:
    // 内部成员变量
    std::string _connect_key;
    std::string _user_name;
    bool _ntrip_version2 = false;
    bool _stopped = false;

    bufferevent *_bev = nullptr;
    evbuffer *_send_evbuf = nullptr;
    std::string _source_list;

public:
    explicit source_ntrip(ConnectInfo info);
    ~source_ntrip();

    int start();
    int stop();

private:
    int build_source_table();

    static void WriteCallback(bufferevent *bev, void *arg);
    static void EventCallback(bufferevent *bev, short events, void *arg);
};