#pragma once
#include "knt.h"
#include "util.h"




class ntrip_server
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

    PROPERTY_AUTO(double, send_total); // 总发送字节数
    PROPERTY_AUTO(double, send_speed); // 总发送速度
    PROPERTY_AUTO(double, recv_total); // 总接收字节数
    PROPERTY_AUTO(double, recv_speed); // 总接收速度
    PROPERTY_AUTO(int64_t, tcp_delay); // TCP延迟

    PROPERTY_AUTO(double, ecef_x);
    PROPERTY_AUTO(double, ecef_y);
    PROPERTY_AUTO(double, ecef_z);
    PROPERTY_AUTO(time_t, position_update_time); // 信息更新时刻

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

    PROPERTY_AUTO(bool,update_flag);

public:
    ntrip_server()
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
        position_update_time(0);

        update_time(0);

        update_flag(false);
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
        info["send_speed"] = send_speed();
        info["recv_total"] = recv_total();
        info["recv_speed"] = recv_speed();
        info["tcp_delay"] = tcp_delay();

        info["ecef_x"] = ecef_x();
        info["ecef_y"] = ecef_y();
        info["ecef_z"] = ecef_z();
        info["position_update_time"] = position_update_time();

        info["update_time"] = update_time();

        info["update_flag"] = update_flag();


        double lat = 0.0, lon = 0.0, alt = 0.0;
        if(position_update_time()!=0) // 证明更新了坐标
        {
            util_ecef2pos(m_ecef_x, m_ecef_y, m_ecef_z, lat, lon, alt);
        }

        info["llh_lat"]=lat;
        info["llh_lon"]= lon;
        info["llh_height"]= alt;

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
        send_speed(info, "send_speed");
        recv_total(info, "recv_total");
        recv_speed(info, "recv_speed");
        tcp_delay(info,"tcp_delay");

        ecef_x(info, "ecef_x");
        ecef_y(info, "ecef_y");
        ecef_z(info, "ecef_z");
        position_update_time(info, "position_update_time");

        update_time(info, "update_time");

        return 0;
    }
};
