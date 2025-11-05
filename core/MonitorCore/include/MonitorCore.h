#pragma once
#include <string>
#include <event2/event.h>
#include "util.h"

#include "context/auth_user.h"
#include "context/ntrip_client.h"
#include "context/ntrip_server.h"

#include "nlohmann/json.hpp"
#include <spdlog/spdlog.h>
using json = nlohmann::json;




class MonitorCore
{
public:
    MonitorCore();

    ~MonitorCore();

    // 返回单例实例
    static MonitorCore *getInstance();

public:

    json genConnectRedisTemp(std::string tempID = ""); // 连接到Redis
    std::string addConnectRedisTask(json info); //

    json genDisConnectRedisTemp(std::string tempID = ""); // 断开Redis
    std::string addDisConnectRedisTask(json info); //

    int addServer(std::string UID, json info);
    int addServer(std::string UID, std::shared_ptr<server_info> obj);
    int delServer(std::string UID);
    int setServer(std::string UID, json info);
    json getServer(const std::string &UID);
    std::shared_ptr<server_info> getServerPtr(const std::string &UID);


    int addClient(std::string UID, json info);
    int addClient(std::string UID, std::shared_ptr<client_info> obj);
    int delClient(std::string UID);
    int setClient(std::string UID, json info);
    json getClient(const std::string &UID);
    std::shared_ptr<client_info> getClientPtr(const std::string &UID);


    int addUser(std::string UID, json info);
    int addUser(std::string UID, std::shared_ptr<user_info> obj);
    int delUser(std::string UID);
    int setUser(std::string UID, json info);
    json getUser(const std::string &UID);
    std::shared_ptr<user_info> getUserPtr(const std::string &UID);


    // 站点遍历回调函数
    void forEachServer(const std::function<void(const std::string &, const std::shared_ptr<server_info> &)> &callback) const;

    // 观测文件遍历回调函数
    void forEachClient(const std::function<void(const std::string &, const std::shared_ptr<client_info> &)> &callback) const;

    // 星历文件遍历回调函数
    void forEachUser(const std::function<void(const std::string &, const std::shared_ptr<user_info> &)> &callback) const;


private:
    std::unordered_map<std::string, std::shared_ptr<server_info>> m_server_map;     //
    std::unordered_map<std::string, std::shared_ptr<client_info>> m_client_map;     //
    std::unordered_map<std::string, std::shared_ptr<user_info>> m_user_map;         //
public:
    // 创建线程,连接至Redis

    // 设置定时函数，定期从Redis中获取数据，这个时间可以灵活更改
    int start();






public:
    int start_server_thread();
    static void *event_base_thread(void *arg);


    int main_task();

    int user_task();

    // 定期任务
    int periodic_task();

    // libevent回调
    static void Request_Process_Cb(evutil_socket_t fd, short what, void *arg);
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

private:
    std::thread _worker; // 线程成员变量

    event_base *_base;
    // process处理事件
    event *_process_event;
    // 定时器和定时事件
    event *_timeout_ev;
    timeval _timeout_tv;

    bool _output_state;
    int _refresh_state_interval=5;
public:
    json _caster_core_setting;
    json _auth_verify_setting;





};
