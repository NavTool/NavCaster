#include "connect_bev.h"
#include "knt.h"
#include "base64.h"
#include "spdlog/spdlog.h"
#include <event2/event.h>

#define __class__ "connect_bev"

connect_bev::connect_bev()
{
}

connect_bev::~connect_bev()
{
}

connect_bev *connect_bev::getInstance()
{
    static connect_bev *instance = new connect_bev();
    return instance;
}

int connect_bev::init(event_base *base)
{
    _base = base;
    return 0;
}

event_base *connect_bev::get_base()
{
    return _base;
}

int connect_bev::add_bev(std::string connect_key, bufferevent *bev)
{
    auto con = _connect_map.find(connect_key);
    if (con != _connect_map.end())
    {
        spdlog::warn("[{}:{}]: connect key already exists in connect_map, connect key: {}, can't add bev", __class__, __func__, connect_key);
        return 1;
    }

    _connect_map.insert(std::pair<std::string, bufferevent *>(connect_key, bev));
    return 0;
}

std::string connect_bev::new_bev(evutil_socket_t fd)
{
    auto connect_key = util_cal_connect_key(fd);
    if (connect_key.size() == 0)
    {
        return connect_key; // 解析fd失败，则表明没有解析出ip和port，可间接表明该连接在解析fd的时候就已经挂了，没必要再进行后续的操作了
    }
    bufferevent *bev = bufferevent_socket_new(_base, fd, BEV_OPT_CLOSE_ON_FREE);

    _connect_map.insert(std::pair<std::string, bufferevent *>(connect_key, bev));
    return connect_key;
}

int connect_bev::del_bev(std::string connect_key)
{
    // 先尝试删除定时器
    del_timer(connect_key);

    // 再删除连接
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        // spdlog::debug("[{}:{}]: free conect, connect key: {}", __class__, __func__, connect_key);
        return 1;
    }

    bufferevent_free(con->second);
    _connect_map.erase(con);

    return 0;
}

int connect_bev::set_key(std::string old_key, std::string new_key)
{
    auto con = _connect_map.find(old_key);
    if (con != _connect_map.end())
    {
        _connect_map.insert(std::pair<std::string, bufferevent *>(new_key, con->second));
        _connect_map.erase(con);
    }
    else
    {
        spdlog::warn("[{}:{}]: con't find bev in connect_map, old key: {}, new key: {}", __class__, __func__, old_key, new_key);
    }

    return 0;
}

bufferevent *connect_bev::get_bev(std::string connect_key)
{
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        spdlog::warn("[{}:{}]: con't find bev in connect_map, connect key: {}", __class__, __func__, connect_key);
        return nullptr;
    }
    return con->second;
}

int connect_bev::set_bev(std::string connect_key, bufferevent_data_cb readcb, bufferevent_data_cb writecb, bufferevent_event_cb eventcb, void *cbarg)
{
    auto bev = get_bev(connect_key);
    if (bev == nullptr)
    {
        spdlog::warn("[{}:{}]: con't find bev in connect_map, connect key: {}, can't set bev", __class__, __func__, connect_key);
        return 1;
    }

    bufferevent_setcb(bev, readcb, writecb, eventcb, cbarg);
    bufferevent_enable(bev, (readcb != nullptr ? EV_READ : 0) | (writecb != nullptr ? EV_WRITE : 0));

    return 0;
}

int connect_bev::set_timer(std::string connect_key, time_t read_timeout_sec, time_t write_timeout_sec)
{
    // 先找到连接
    auto bev = get_bev(connect_key); // 连接不存在，直接返回错误
    if (bev == nullptr)
    {
        spdlog::warn("[{}:{}]: con't find bev in connect_map, connect key: {}, can't set timer", __class__, __func__, connect_key);
        return 1;
    }

    auto read_timer_item = _read_timer_map.find(connect_key);
    if (read_timer_item != _read_timer_map.end())
    {
        read_timer_item->second->tv_sec = read_timeout_sec;
        read_timer_item->second->tv_usec = 0;
    }
    else
    {
        auto timer = new timeval;
        timer->tv_sec = read_timeout_sec;
        timer->tv_usec = 0;
        _read_timer_map.insert(std::pair<std::string, timeval *>(connect_key, timer));
    }

    auto write_timer_item = _write_timer_map.find(connect_key);
    if (write_timer_item != _write_timer_map.end())
    {
        write_timer_item->second->tv_sec = write_timeout_sec;
        write_timer_item->second->tv_usec = 0;
    }
    else
    {
        auto timer = new timeval;
        timer->tv_sec = write_timeout_sec;
        timer->tv_usec = 0;
        _write_timer_map.insert(std::pair<std::string, timeval *>(connect_key, timer));
    }

    auto read_timer = _read_timer_map.find(connect_key)->second;
    auto write_timer = _write_timer_map.find(connect_key)->second;
    if (read_timeout_sec <= 0)
    {
        read_timer = NULL;
    }
    if (write_timeout_sec <= 0)
    {
        write_timer = NULL;
    }

    bufferevent_set_timeouts(bev, read_timer, write_timer);

    return 0;
}

int connect_bev::del_timer(std::string connect_key)
{
    // 先找到连接
    auto bev = get_bev(connect_key); // 连接不存在，直接返回错误
    if (bev == nullptr)
    {
        spdlog::warn("[{}:{}]: con't find bev in connect_map, connect key: {}, can't set timer", __class__, __func__, connect_key);
        return 1;
    }

    bufferevent_set_timeouts(bev, NULL, NULL); // 清除定时器

    auto read_timer_item = _read_timer_map.find(connect_key);
    if (read_timer_item != _read_timer_map.end())
    {
        delete read_timer_item->second;
        _read_timer_map.erase(read_timer_item);
    }
    auto write_timer_item = _write_timer_map.find(connect_key);
    if (write_timer_item != _write_timer_map.end())
    {
        delete write_timer_item->second;
        _write_timer_map.erase(write_timer_item);
    }
    return 0;
}
