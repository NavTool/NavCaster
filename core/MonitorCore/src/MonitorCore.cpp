#include "MonitorCore.h"

#include "Caster_Core.h"
#include "Auth_Verify.h"
#include "event2/thread.h"

MonitorCore::MonitorCore()
{

#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        spdlog::info("WSAStartup failed! exit.");
        // return 1;
    }
#endif




#ifdef _WIN32
    if (evthread_use_windows_threads() != 0) {
        fprintf(stderr, "Failed to initialize libevent thread support (Windows)\n");
    }
#else
    if (evthread_use_pthreads() != 0) {
        fprintf(stderr, "Failed to initialize libevent thread support (POSIX)\n");
    }
#endif

    _base = event_base_new();

    _timeout_tv.tv_sec = _refresh_state_interval;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);



}

MonitorCore::~MonitorCore()
{
#ifdef WIN32
    WSACleanup();
#endif

}

MonitorCore *MonitorCore::getInstance()
{
    static MonitorCore *instance = new MonitorCore();
    return instance;
}

json MonitorCore::genConnectRedisTemp(std::string tempID)
{
    json item;
    item["type"] = "ReadNav_Opt";


    item["navfile_UID"] = "";
    item["navfile_name"] = "";
    item["navfile_path"] = "";


    item["utc_stamp"]=0;  //数据采集的UTC时间，需要手动设置

    // info["items"].push_back(item);

    return item;
}

std::string MonitorCore::addConnectRedisTask(json info)
{
    std::string UID;


    return UID;
}

json MonitorCore::genDisConnectRedisTemp(std::string tempID)
{
    json item;

    return item;
}

std::string MonitorCore::addDisConnectRedisTask(json info)
{
    std::string UID;


    return UID;
}

int MonitorCore::start()
{


    //  Init模块

    // 初始化用户模块
    AUTH::Init(_auth_verify_setting.dump().c_str(), _base);

    // 初始化Caster_Core
    CASTER::Init(_caster_core_setting.dump().c_str(), _base);

    // 创建listener请求

    // 定时器事件，定时从两个模块中拉取数据到本地加锁内存中，



    // 添加超时事件
    event_add(_timeout_ev, &_timeout_tv);
    // 启动event_base处理线程
    start_server_thread();






    return 0;
}

int MonitorCore::start_server_thread()
{

    _worker= std::thread(&MonitorCore::event_base_thread, _base);
    _worker.detach();

    // event_base_thread(_base);
    return 0;
}


void *MonitorCore::event_base_thread(void *arg)
{


    event_base *base = static_cast<event_base *>(arg);
    evthread_make_base_notifiable(base);

    spdlog::info("Server is runing...");
    event_base_dispatch(base);

    spdlog::warn("Server is stop!"); // 不应当主动发生



    return nullptr;
}

int MonitorCore::main_task()
{
    // 如果连接上了Redis  那就刷新数据


    // 如果没连接上Redis，那就根据状态来决定要干什么

    //如果没有连接任务，那就什么都不做

    // 如果有连接任务，那就创建Redis连接，如果正在连接，那就等待，刷新状态信息

    return 0;
}

int MonitorCore::user_task()
{
    // 用户可以执行的一些操作







    return 0;



}


int MonitorCore::periodic_task()
{
    auto base=CASTER::Get_Active_Base_UID();
    for(auto iter:base)
    {

        std::string str=CASTER::Get_Base_Info(iter);

        if(str=="")
        {
            continue;
        }

        json info=json::parse(str);

        /*{
         * "ip":"127.0.0.1",
         * "mount_point":"SSRA03IGS0_SIRGAS2000",
         * "online_seconds":119,
         * "online_time":1762162232,
         * "port":34976,
         * "recv_speed":3668.0,
         * "recv_total":60970,
         * "send_speed":0.0,
         * "send_total":0,
         * "update_time":1762162352,
         * "user_name":"none"
         * }
        */
        std::shared_ptr<server_info> item=std::make_shared<server_info>();

        item->UID(iter);
        item->login_mpt(info["mount_point"]);
        item->alias_mpt(info["mount_point"]);
        item->type(1);
        item->account(info["user_name"]);
        item->ip(info["ip"]);
        item->port(info["port"]);
        item->online_time(info["online_time"]);
        item->online_seconds(info["online_seconds"]);

        item->send_total(info["send_total"]);
        item->send_speed(info["send_speed"]);
        item->recv_total(info["recv_total"]);
        item->recv_speed(info["recv_speed"]);

        item->llh_lat(0.0);
        item->llh_lon(0.0);
        item->llh_h(0.0);

        item->update_time(info["update_time"]);

        m_server_map.insert(std::pair(iter,item));
    }
    spdlog::info("active base count: {}",base.size());



    auto rover=CASTER::Get_Active_Rover_UID();
    for(auto iter:rover)
    {

        std::string str=CASTER::Get_Rover_Info(iter);

        if(str=="")
        {
            continue;
        }

        json info=json::parse(str);

        std::shared_ptr<client_info> item=std::make_shared<client_info>();

        item->UID(iter);
        item->login_mpt(info["mount_point"]);
        item->inter_mpt(info["mount_point"]);
        item->type(1);
        item->account(info["user_name"]);
        item->ip(info["ip"]);
        item->port(info["port"]);
        item->online_time(info["online_time"]);
        item->online_seconds(info["online_seconds"]);

        item->send_total(info["send_total"]);
        item->send_speed(info["send_speed"]);
        item->recv_total(info["recv_total"]);
        item->recv_speed(info["recv_speed"]);

        item->llh_lat(0.0);
        item->llh_lon(0.0);
        item->llh_h(0.0);

        item->update_time(info["update_time"]);

        m_client_map.insert(std::pair(iter,item));
    }
    spdlog::info("active rover count: {}",rover.size());



    return 0;
}

void MonitorCore::Request_Process_Cb(intptr_t fd, short what, void *arg)
{

}

void MonitorCore::TimeoutCallback(intptr_t fd, short events, void *arg)
{
    auto *svr = static_cast<MonitorCore *>(arg);
    svr->periodic_task();
}

int MonitorCore::addServer(std::string UID, json info)
{
    return 0;
}

int MonitorCore::addServer(std::string UID, std::shared_ptr<server_info> obj)
{
    return 0;
}

int MonitorCore::delServer(std::string UID)
{
    return 0;
}

int MonitorCore::setServer(std::string UID, json info)
{
    return 0;
}

json MonitorCore::getServer(const std::string &UID)
{
    return json();
}

std::shared_ptr<server_info> MonitorCore::getServerPtr(const std::string &UID)
{
    return nullptr;
}

int MonitorCore::addClient(std::string UID, json info)
{
    return 0;
}

int MonitorCore::addClient(std::string UID, std::shared_ptr<client_info> obj)
{
    return 0;
}

int MonitorCore::delClient(std::string UID)
{
    return 0;
}

int MonitorCore::setClient(std::string UID, json info)
{
    return 0;
}

json MonitorCore::getClient(const std::string &UID)
{
    return json();
}

std::shared_ptr<client_info> MonitorCore::getClientPtr(const std::string &UID)
{
    return nullptr;
}

int MonitorCore::addUser(std::string UID, json info)
{
    return 0;
}

int MonitorCore::addUser(std::string UID, std::shared_ptr<user_info> obj)
{
    return 0;
}

int MonitorCore::delUser(std::string UID)
{
    return 0;
}

int MonitorCore::setUser(std::string UID, json info)
{
    return 0;
}

json MonitorCore::getUser(const std::string &UID)
{
    return json();
}

std::shared_ptr<user_info> MonitorCore::getUserPtr(const std::string &UID)
{
    return nullptr;
}

void MonitorCore::forEachServer(const std::function<void (const std::string &, const std::shared_ptr<server_info> &)> &callback) const
{
    for (const auto &[key, st] : m_server_map)
    {
        callback(key, st);
    }
}

void MonitorCore::forEachClient(const std::function<void (const std::string &, const std::shared_ptr<client_info> &)> &callback) const
{
    for (const auto &[key, st] : m_client_map)
    {
        callback(key, st);
    }
}

void MonitorCore::forEachUser(const std::function<void (const std::string &, const std::shared_ptr<user_info> &)> &callback) const
{
    for (const auto &[key, st] : m_user_map)
    {
        callback(key, st);
    }
}
