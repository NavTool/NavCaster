#pragma once
#include "knt.h"
#include "util.h"

class ntrip_client{

private:
    PROPERTY_AUTO(std::string,UID);        // TCP连接唯一标识
    PROPERTY_AUTO(std::string,login_mpt);  // 接入的挂载点
    PROPERTY_AUTO(std::string,alias_mpt);  // 内部提供数据的挂载点（真正使用的挂载点）

    PROPERTY_AUTO(int, type);              /* 接入类型
                             *  0：未知
                             *  1：普通接入模式
                             *  2：最近基站模式
                             *  3：Push数据(Ntrip Server)
                             *  4：Push数据(TCP Client)
                             *  5：Push数据(TCP Server)
                             *  6：Proxy模式
                             */

    PROPERTY_AUTO(std::string,account);
    PROPERTY_AUTO(std::string,ip);
    PROPERTY_AUTO(int, port);
    PROPERTY_AUTO(time_t, online_time); // 上线时刻
    PROPERTY_AUTO(time_t, online_seconds); // 上线持续时间

    PROPERTY_AUTO(double, send_total); // 总发送字节数
    PROPERTY_AUTO(double, send_speed); // 总发送速度
    PROPERTY_AUTO(double, recv_total); // 总接收字节数
    PROPERTY_AUTO(double, recv_speed); // 总接收速度
    PROPERTY_AUTO(int64_t, tcp_delay); // TCP延迟

    PROPERTY_AUTO(double, ecef_x);
    PROPERTY_AUTO(double, ecef_y);
    PROPERTY_AUTO(double, ecef_z);
    PROPERTY_AUTO(int, quality);
    PROPERTY_AUTO(int, sat_num);
    PROPERTY_AUTO(double, diff);
    PROPERTY_AUTO(double, distance);
    PROPERTY_AUTO(time_t, position_update_time); // 信息更新时刻

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

    PROPERTY_AUTO(bool,update_flag);

public:
    ntrip_client()
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
        send_speed(0.0);
        recv_total(0);
        recv_speed(0.0);
        tcp_delay(0);

        ecef_x(0.0);
        ecef_y(0.0);
        ecef_z(0.0);
        quality(0);
        sat_num(0);
        diff(0.0);
        distance(0.0);
        position_update_time(0);

        update_time(0);

        update_flag(false);
    }

    json info()
    {
        json info;
        info["uid"] = UID();
        info["login_mpt"] = login_mpt();
        info["alias_mpt"] = alias_mpt();

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
        info["tcp_delay"] = tcp_delay();

        info["ecef_x"] = ecef_x();
        info["ecef_y"] = ecef_y();
        info["ecef_z"] = ecef_z();
        info["quality"] = quality();
        info["sat_num"] = sat_num();
        info["diff"] = diff();
        info["distance"] = distance();
        info["position_update_time"] = position_update_time();

        info["update_time"] = update_time();

        info["update_flag"] = update_flag();

        return info;
    }

    int setInfo(json info)
    {

        UID(info, "uid");
        login_mpt(info, "login_mpt");
        alias_mpt(info, "alias_mpt");

        type(info, "type");
        account(info, "account");
        ip(info, "ip");
        port(info, "port");
        online_time(info, "online_time");
        online_seconds(info, "online_seconds");

        // send/recv stats moved to StreamState proto, not in ClientState
        // send_total(info, "send_total");
        // send_speed(info, "send_speed");
        // recv_total(info, "recv_total");
        // recv_speed(info, "recv_speed");
        tcp_delay(info,"tcp_delay");

        ecef_x(info, "ecef_x");
        ecef_y(info, "ecef_y");
        ecef_z(info, "ecef_z");
        quality(info, "quality");
        sat_num(info, "sat_num");
        diff(info, "diff");
        position_update_time(info, "position_update_time");

        update_time(info, "update_time");

        return 0;
    }

};
