/*
    relay_pull.h - non-coroutine relay pull session.
*/
#pragma once

#include "ntrip_msg.h"

#include <string>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

class relay_pull
{
private:
    //  固定信息
    const CasterRegisterType _register_type = CasterRegisterType::PULL;
    std::string __class__ = "relay_pull";

private:
    // 传递信息
    ConnectInfo _info;

private:
    // 内部成员变量
    enum class State
    {
        Idle,
        Connecting,
        Handshaking,
        Registering,
        Running,
        WaitingRetry
    };

    std::string _task_key;
    std::string _connect_key;
    std::string _mount_point;
    std::string _user_name;
    bool _ntrip_version2 = false;
    bool _transfer_with_chunked = false;
    size_t _chunked_size = 0;
    bool _stopped = false;
    bool _registered = false;
    int _retry_delay = 5;
    State _state = State::Idle;

    bufferevent *_bev = nullptr;
    evbuffer *_recv_evbuf = nullptr;
    event *_timeout_ev = nullptr;
    timeval _timeout_tv{};
    bool _timeout_ev_flag = false;

public:
    explicit relay_pull(ConnectInfo info);
    ~relay_pull();

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
    int publish_recv_raw_data();
    int publish_data_from_evbuf();
    int publish_data_from_chunk();

    static void ReadCallback(bufferevent *bev, void *arg);
    static void EventCallback(bufferevent *bev, short events, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);
    static void CasterRegisterCallback(const char *request, void *arg, caster_reply *reply);
};