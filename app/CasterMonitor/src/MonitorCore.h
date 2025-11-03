#pragma once
#include <string>
#include <event2/event.h>
#include "util.h"
#include "nlohmann/json.hpp"
#include <spdlog/spdlog.h>
using json = nlohmann::json;

class server_info
{
private:
    PROPERTY_AUTO(std::string,UID);        // TCP连接唯一标识
    PROPERTY_AUTO(std::string,login_mpt);  // 接入的挂载点
    PROPERTY_AUTO(std::string,alias_mpt);  // 对外服务的挂载点

    PROPERTY_AUTO(int, type);             /* 挂载点类型
                             *  0：未知
                             *  1：普通挂载点
                             *  2：最近挂载点
                             *  3：Relay挂载点（Ntrip Client）
                             *  4：Relay挂载点（TCP Client）
                             *  5：Relay挂载点（TCP Server）
                             *  6：Proxy挂载点（Ntrip Client）
                             *  7：Alias挂载点（挂载点添加一个别名，可通过这个别名来获取数据）
                             */

    PROPERTY_AUTO(std::string,account);
    PROPERTY_AUTO(std::string,ip);
    PROPERTY_AUTO(int, port);
    PROPERTY_AUTO(time_t, online_time); // 上线时刻
    PROPERTY_AUTO(time_t, online_seconds); // 上线持续时间

    PROPERTY_AUTO(int64_t, send_total); // 总发送字节数
    PROPERTY_AUTO(int64_t, send_count);
    PROPERTY_AUTO(double, send_speed); // 总发送速度
    PROPERTY_AUTO(int64_t, recv_total); // 总接收字节数
    PROPERTY_AUTO(int64_t, recv_count);
    PROPERTY_AUTO(double, recv_speed); // 总接收速度

    PROPERTY_AUTO(double, llh_lat);
    PROPERTY_AUTO(double, llh_lon);
    PROPERTY_AUTO(double, llh_h);

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

public:
    server_info()
    {
        UID("");
        login_mpt("");
        alias_mpt("");

        type(0);
        account("");
        ip("");
        port(0);
        online_time(0);
        online_seconds(0);

        send_total(0);
        send_count(0);
        send_speed(0.0);
        recv_total(0);
        recv_count(0);
        recv_speed(0.0);

        llh_lat(0.0);
        llh_lon(0.0);
        llh_h(0.0);

        update_time(0);
    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["login_mpt"] = login_mpt();
        info["alias_mpt"] = alias_mpt();

        info["type"] = type();
        info["account"] = account();
        info["ip"] = ip();
        info["port"] = port();
        info["online_time"] = online_time();
        info["online_seconds"] = online_seconds();

        info["send_total"] = send_total();
        info["send_count"] = send_count();
        info["send_speed"] = send_speed();
        info["recv_total"] = recv_total();
        info["recv_count"] = recv_count();
        info["recv_speed"] = recv_speed();

        info["llh_lat"] = llh_lat();
        info["llh_lon"] = llh_lon();
        info["llh_h"] = llh_h();

        info["update_time"] = update_time();
        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        login_mpt(info, "login_mpt");
        alias_mpt(info, "alias_mpt");

        type(info, "type");
        account(info, "account");
        ip(info, "ip");
        port(info, "port");
        online_time(info, "online_time");
        online_seconds(info, "online_seconds");

        send_total(info, "send_total");
        send_count(info, "send_count");
        send_speed(info, "send_speed");
        recv_total(info, "recv_total");
        recv_count(info, "recv_count");
        recv_speed(info, "recv_speed");

        llh_lat(info, "llh_lat");
        llh_lon(info, "llh_lon");
        llh_h(info, "llh_h");

        update_time(info, "update_time");

        return 0;
    }
};

class client_info{

private:
    PROPERTY_AUTO(std::string,UID);        // TCP连接唯一标识
    PROPERTY_AUTO(std::string,login_mpt);  // 接入的挂载点
    PROPERTY_AUTO(std::string,inter_mpt);// 内部提供数据的挂载点（真正使用的挂载点）

    PROPERTY_AUTO(int, type);              /* 接入类型
                             *  0：未知
                             *  1：普通接入模式
                             *  2：最近基站模式
                             *  3：Proxy模式
                             */

    PROPERTY_AUTO(std::string,account);
    PROPERTY_AUTO(std::string,ip);
    PROPERTY_AUTO(int, port);
    PROPERTY_AUTO(time_t, online_time); // 上线时刻
    PROPERTY_AUTO(time_t, online_seconds); // 上线持续时间

    PROPERTY_AUTO(int64_t, send_total); // 总发送字节数
    PROPERTY_AUTO(int64_t, send_count);
    PROPERTY_AUTO(double, send_speed); // 总发送速度
    PROPERTY_AUTO(int64_t, recv_total); // 总接收字节数
    PROPERTY_AUTO(int64_t, recv_count);
    PROPERTY_AUTO(double, recv_speed); // 总接收速度

    PROPERTY_AUTO(double, llh_lat);
    PROPERTY_AUTO(double, llh_lon);
    PROPERTY_AUTO(double, llh_h);

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

 public:
    client_info()
    {
        UID("");
        login_mpt("");
        inter_mpt("");

        type(0);
        account("");
        ip("");
        port(0);
        online_time(0);
        online_seconds(0);

        send_total(0);
        send_count(0);
        send_speed(0.0);
        recv_total(0);
        recv_count(0);
        recv_speed(0.0);

        llh_lat(0.0);
        llh_lon(0.0);
        llh_h(0.0);

        update_time(0);
    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["login_mpt"] = login_mpt();
        info["inter_mpt"] = inter_mpt();

        info["type"] = type();
        info["account"] = account();
        info["ip"] = ip();
        info["port"] = port();
        info["online_time"] = online_time();
        info["online_seconds"] = online_seconds();

        info["send_total"] = send_total();
        info["send_count"] = send_count();
        info["send_speed"] = send_speed();
        info["recv_total"] = recv_total();
        info["recv_count"] = recv_count();
        info["recv_speed"] = recv_speed();

        info["llh_lat"] = llh_lat();
        info["llh_lon"] = llh_lon();
        info["llh_h"] = llh_h();

        info["update_time"] = update_time();
        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        login_mpt(info, "login_mpt");
        inter_mpt(info, "inter_mpt");

        type(info, "type");
        account(info, "account");
        ip(info, "ip");
        port(info, "port");
        online_time(info, "online_time");
        online_seconds(info, "online_seconds");

        send_total(info, "send_total");
        send_count(info, "send_count");
        send_speed(info, "send_speed");
        recv_total(info, "recv_total");
        recv_count(info, "recv_count");
        recv_speed(info, "recv_speed");

        llh_lat(info, "llh_lat");
        llh_lon(info, "llh_lon");
        llh_h(info, "llh_h");

        update_time(info, "update_time");

        return 0;
    }

};

class user_info{

private:
    PROPERTY_AUTO(std::string, UID);      // 账户名  如果勾选了Ntrip1.0的基站，那么UID会是密码，其他情况下，UID是用户名
    PROPERTY_AUTO(std::string, account);  // 用户名
    PROPERTY_AUTO(std::string, passowrd); // 密码

    PROPERTY_AUTO(int, type);             /* 账号类型
                             * 0：未知
                             * 1：期限账号
                             * 2：永久账号
                             * 3：机构账号（一个账号可登录多次）
                             */

    PROPERTY_AUTO(int, state);            /* 账号状态
                             * 0：未知
                             * 1：未启用
                             * 2：已启用
                             * 3：已停用
                             * 4：已过期
                             * 5：已注销
                             * （已删除）
                             */

    PROPERTY_AUTO(int, access);           /* 接入类型
                             * 0x00：无权限
                             * 0x01：允许以基站模式接入（Ntrip1.0）（只验证密码）
                             * 0x01：允许以基站模式接入（Ntrip2.0）
                             * 0x02：允许获取实体挂载点数据
                             * 0x04：允许获取最近挂载点数据
                             * 0x08：允许获取Alias挂载点数据（只能获取有限挂载点数据）
                             * 0x10：允许获取Proxy挂载点数据
                             */

    PROPERTY_AUTO(time_t, register_time);   // 注册时间
    PROPERTY_AUTO(time_t, active_time);     // 激活时间
    PROPERTY_AUTO(time_t, expired_time);    // 过期时间

    PROPERTY_AUTO(int, access_limit);       //允许连接数量 //只有机构账号才会生效

    PROPERTY_AUTO(std::string, userName);           // 用户名
    PROPERTY_AUTO(std::string, contactPerson);      // 联系人
    PROPERTY_AUTO(std::string, contactInfo);        // 联系方式
    PROPERTY_AUTO(std::string, organizationName);   // 所属机构
    PROPERTY_AUTO(std::string, remark);             // 备注信息

    PROPERTY_AUTO(time_t, modifiedDate);    // 记录更新时间


public:
    user_info()
    {
        UID("");
        account("");
        passowrd("");

        type(0);
        state(0);
        access(0);

        register_time(0);
        active_time(0);
        expired_time(0);

        access_limit(0);

        userName("");
        contactPerson("");
        contactInfo("");
        organizationName("");
        remark("");

        modifiedDate(0);

    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["account"] = account();
        info["passowrd"] = passowrd();

        info["type"] = type();
        info["state"] = state();
        info["access"] = access();

        info["register_time"] = register_time();
        info["active_time"] = active_time();
        info["expired_time"] = expired_time();

        info["access_limit"] = access_limit();

        info["userName"] = userName();
        info["contactPerson"] = contactPerson();
        info["contactInfo"] = contactInfo();
        info["organizationName"] = organizationName();
        info["remark"] = remark();

        info["modifiedDate"] = modifiedDate();

        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        account(info, "account");
        passowrd(info, "passowrd");

        type(info, "type");
        state(info, "state");
        access(info, "access");

        register_time(info, "register_time");
        active_time(info, "active_time");
        expired_time(info, "expired_time");

        access_limit(info, "access_limit");

        userName(info, "userName");
        contactPerson(info, "contactPerson");
        contactInfo(info, "contactInfo");
        organizationName(info, "organizationName");
        remark(info, "remark");

        modifiedDate(info, "modifiedDate");

        return 0;
    }

};



class MonitorCore
{
public:
    MonitorCore();

    // 返回单例实例
    static MonitorCore *getInstance();



public:



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
