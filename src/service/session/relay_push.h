/*
    relay_push.h - non-coroutine relay push session.
*/
#pragma once

#include "ntrip_msg.h"

#include <string>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

class relay_push
{
private:
    //  固定信息
    const CasterRegisterType _register_type = CasterRegisterType::PUSH;
    std::string __class__ = "relay_push";

private:
    // 传递信息
    ConnectInfo _info;

private:
    enum class State
    {
        Idle,
        Connecting,
        Handshaking,
        Registering,
        Subscribing,
        Running,
        WaitingRetry
    };

    std::string _connect_key;
    std::string _mount_point;
    std::string _user_name;
    bool _ntrip_version2 = false;
    bool _transfer_with_chunked = false;
    bool _stopped = false;
    bool _registered = false;
    bool _subscribed = false;
    int _retry_delay = 5;
    State _state = State::Idle;

    bufferevent *_bev = nullptr;
    evbuffer *_send_evbuf = nullptr;
    evbuffer *_recv_evbuf = nullptr;
    event *_timeout_ev = nullptr;
    timeval _timeout_tv{};
    bool _timeout_ev_flag = false;

public:
    explicit relay_push(ConnectInfo info);
    ~relay_push();

    int start();
    int stop();

private:
    void backoff();
    void reset_backoff();
    int start_connect();
    int handle_connected();
    int handle_handshake();
    int running();
    int cleanup_connection();
    int schedule_retry(const std::string &reason);
    int transfer_sub_raw_data(const char *data, size_t length);

    static void ReadCallback(bufferevent *bev, void *arg);
    static void EventCallback(bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);
    static void CasterRegisterCallback(const char *request, void *arg, caster_reply *reply);
    static void CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply);
};