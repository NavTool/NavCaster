/*
    client_near.h - non-coroutine nearest-rover session.
*/
#pragma once

#include "ntrip_msg.h"

#include <string>

#include <event2/buffer.h>
#include <event2/bufferevent.h>

class client_near
{
private:
    //  固定信息
    const CasterRegisterType _register_type = CasterRegisterType::NEAREST;
    const AuthType _auth_type = AuthType::CLIENT;
    std::string __class__ = "client_near";

private:
    // 传递信息
    ConnectInfo _info;

private:
    // 内部成员变量
    std::string _connect_key;
    std::string _mount_point;
    std::string _alias_mpt;   // 实际订阅的挂载点名（切换后的真实名）
    std::string _user_name;
    bool _ntrip_version2 = false;
    bool _transfer_with_chunked = false;
    bool _stopped = false;
    bool _registered = false;
    bool _running = false;

    bufferevent *_bev = nullptr;
    evbuffer *_send_evbuf = nullptr;
    evbuffer *_recv_evbuf = nullptr;

public:
    explicit client_near(ConnectInfo info);
    ~client_near();

    int start();
    int stop();

private:
    int running();
    int send_reply();
    int transfer_sub_raw_data(const char *data, size_t length);
    int publish_recv_raw_data();
    int subscribe_initial_position();

    static void ReadCallback(bufferevent *bev, void *arg);
    static void EventCallback(bufferevent *bev, short events, void *arg);
    static void AuthLoginCallback(const char *request, void *arg, auth_reply *reply);
    static void CasterRegisterCallback(const char *request, void *arg, caster_reply *reply);
    static void CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply);
};