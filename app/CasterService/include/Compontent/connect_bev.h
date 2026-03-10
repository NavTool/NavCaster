#pragma once
#include <string>
#include <unordered_map>
#include "event2/bufferevent.h"

class connect_bev
{
private:
    event_base *_base;

    std::unordered_map<std::string, bufferevent *> _connect_map; // Connect_Key,bev
    std::unordered_map<std::string, timeval *> _read_timer_map;
    std::unordered_map<std::string, timeval *> _write_timer_map;

public:
    connect_bev(/* args */);
    ~connect_bev();

    static connect_bev *getInstance();

    int init(event_base *base);

    event_base *get_base();

    int add_bev(std::string connect_key, bufferevent *bev);
    std::string new_bev(evutil_socket_t fd);
    bufferevent *get_bev(std::string connect_key);
    int set_bev(std::string connect_key, bufferevent_data_cb readcb, bufferevent_data_cb writecb,
                bufferevent_event_cb eventcb, void *cbarg);
    int del_bev(std::string connect_key);
    int set_key(std::string old_key, std::string new_key);

    int set_timer(std::string connect_key, time_t read_timeout_sec, time_t write_timeout_sec);
    int del_timer(std::string connect_key);
};
