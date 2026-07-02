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
    static connect_bev instance;
    return &instance;
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

std::string connect_bev::new_bev(std::string addr, int port)
{
    evutil_addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    int gai_ret = evutil_getaddrinfo(addr.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (gai_ret != 0 || res == nullptr)
    {
        spdlog::error("[{}:{}]: DNS resolve failed for {}:{}, error: {}", __class__, __func__, addr, port, gai_ret != 0 ? evutil_gai_strerror(gai_ret) : "null result");
        if (res)
            evutil_freeaddrinfo(res);
        return std::string();
    }

    // 创建一个绑定在base上的buffevent，并建立socket连接
    auto bev = bufferevent_socket_new(_base, -1, BEV_OPT_CLOSE_ON_FREE); //-1表示自动创建fd
    if (bufferevent_socket_connect(bev, res->ai_addr, res->ai_addrlen))
    {
        // 连接建立失败
        spdlog::warn("[{}:{}]: socket connect failed for {}:{}", __class__, __func__, addr, port);
        bufferevent_free(bev);
        evutil_freeaddrinfo(res);
        return std::string();
    }
    evutil_freeaddrinfo(res);

    // 连接建立成功。返回port，连接建立失败，返回0
    auto fd = bufferevent_getfd(bev);
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (getsockname(fd, (struct sockaddr *)&sa, &len))
    {
        // 获取本地连接信息失败
        bufferevent_free(bev);
        return std::string();
    }

    auto connect_key = util_cal_connect_key(fd);
    if (connect_key.empty())
    {
        // 这个时候连接还没有建立成功，所以可能还解析不出来
        connect_key = util_generate_random_key(16);
    }
    _connect_map.insert(std::pair<std::string, bufferevent *>(connect_key, bev));
    return connect_key;
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

std::string connect_bev::recalculate_key(const std::string &old_key)
{
    auto con = _connect_map.find(old_key);
    if (con == _connect_map.end())
        return old_key;

    int fd = bufferevent_getfd(con->second);
    auto new_key = util_cal_connect_key(fd);
    if (new_key.empty() || new_key == old_key)
        return old_key;

    // 更新 map 中的 key
    _connect_map.insert({new_key, con->second});
    _connect_map.erase(con);
    return new_key;
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
        auto timer = std::make_unique<timeval>();
        timer->tv_sec = read_timeout_sec;
        timer->tv_usec = 0;
        _read_timer_map.emplace(connect_key, std::move(timer));
    }

    auto write_timer_item = _write_timer_map.find(connect_key);
    if (write_timer_item != _write_timer_map.end())
    {
        write_timer_item->second->tv_sec = write_timeout_sec;
        write_timer_item->second->tv_usec = 0;
    }
    else
    {
        auto timer = std::make_unique<timeval>();
        timer->tv_sec = write_timeout_sec;
        timer->tv_usec = 0;
        _write_timer_map.emplace(connect_key, std::move(timer));
    }

    timeval *read_timer = _read_timer_map.find(connect_key)->second.get();
    timeval *write_timer = _write_timer_map.find(connect_key)->second.get();
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

    _read_timer_map.erase(connect_key);
    _write_timer_map.erase(connect_key);
    return 0;
}
