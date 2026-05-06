/*
    server_ntrip.h - non-coroutine base-station session.
*/
#pragma once

#include "ntrip_msg.h"

#include <string>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

class server_ntrip
{
private:
    //  固定信息
    const CasterRegisterType _register_type = CasterRegisterType::SERVER;
    const AuthType _auth_type = AuthType::SERVER;
    std::string __class__ = "server_ntrip";
private:
    // 传递信息
        ConnectInfo _info;
        
private:
    // 内部成员变量
    std::string _connect_key;
    std::string _mount_point;
    std::string _user_name;
    bool _ntrip_version2 = false;
    bool _transfer_with_chunked = false;
    size_t _chunked_size = 0;
    bool _stopped = false;
    bool _registered = false;


    bufferevent *_bev = nullptr;
    evbuffer *_send_evbuf = nullptr;
    evbuffer *_recv_evbuf = nullptr;
    event *_timeout_ev = nullptr;
    timeval _timeout_tv{};
    bool _timeout_ev_flag = false;


public:
    explicit server_ntrip(ConnectInfo info);
    ~server_ntrip();

    int start();
    int stop();

private:


    int running();
    int send_reply();
    int send_heart_beat_to_server();
    int publish_recv_raw_data();
    int publish_data_from_evbuf();
    int publish_data_from_chunk();

    static void ReadCallback(bufferevent *bev, void *arg);
    static void EventCallback(bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);
    static void AuthLoginCallback(const char *request, void *arg, auth_reply *reply);
    static void CasterRegisterCallback(const char *request, void *arg, caster_reply *reply);
};