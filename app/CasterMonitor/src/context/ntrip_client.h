#pragma once
#include "util.h"

class ntrip_client{

private:
    PROPERTY_AUTO(std::string,UID);        // TCP连接唯一标识
    PROPERTY_AUTO(std::string,login_mpt);  // 接入的挂载点
    PROPERTY_AUTO(std::string,inter_mpt);  // 内部提供数据的挂载点（真正使用的挂载点）

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
    PROPERTY_AUTO(double, send_speed); // 总发送速度
    PROPERTY_AUTO(int64_t, recv_total); // 总接收字节数
    PROPERTY_AUTO(double, recv_speed); // 总接收速度

    PROPERTY_AUTO(double, llh_lat);
    PROPERTY_AUTO(double, llh_lon);
    PROPERTY_AUTO(double, llh_h);
    PROPERTY_AUTO(time_t, position_update_time); // 信息更新时刻

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

 public:
    ntrip_client()
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
        send_speed(0.0);
        recv_total(0);
        recv_speed(0.0);

        llh_lat(0.0);
        llh_lon(0.0);
        llh_h(0.0);
        position_update_time(0);

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
        info["send_speed"] = send_speed();
        info["recv_total"] = recv_total();
        info["recv_speed"] = recv_speed();

        info["llh_lat"] = llh_lat();
        info["llh_lon"] = llh_lon();
        info["llh_h"] = llh_h();
        info["position_update_time"] = position_update_time();

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
        send_speed(info, "send_speed");
        recv_total(info, "recv_total");
        recv_speed(info, "recv_speed");

        llh_lat(info, "llh_lat");
        llh_lon(info, "llh_lon");
        llh_h(info, "llh_h");
        position_update_time(info, "position_update_time");

        update_time(info, "update_time");

        return 0;
    }

};
