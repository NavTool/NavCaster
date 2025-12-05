#pragma once
#include "util.h"


class caster_node{

private:
    PROPERTY_AUTO(std::string, UID);        // 唯一标识+机器码+监听端口
    PROPERTY_AUTO(std::string, node_name)   // 节点名称
    PROPERTY_AUTO(std::string,set_version); // 版本
    PROPERTY_AUTO(std::string,tag_version); // 版本
    PROPERTY_AUTO(std::string,run_platform); // 平台

    PROPERTY_AUTO(double,cpu_usage);   // CPU占用
    PROPERTY_AUTO(int,mem_usage);      // 内存占用
    PROPERTY_AUTO(int,queue_delay);      // 队列平均执行延迟
    PROPERTY_AUTO(int,sub_ping_delay);      // SUB连接平均执行延迟
    PROPERTY_AUTO(int,sub_tcp_delay);      // SUB连接平均执行延迟
    PROPERTY_AUTO(int,pub_ping_delay);      // PUB队列平均执行延迟
    PROPERTY_AUTO(int,pub_tcp_delay);      // PUB队列平均执行延迟
    PROPERTY_AUTO(double,send_total);      // 累计发送
    PROPERTY_AUTO(double,send_speed);      // 发送速度
    PROPERTY_AUTO(double,recv_total);      // 累计接收
    PROPERTY_AUTO(double,recv_speed);      // 接收速度


    PROPERTY_AUTO(int,connnect_count);      // 连接数
    PROPERTY_AUTO(int,server_count);        // 基站个数
    PROPERTY_AUTO(int,client_count);        // 移动站个数


    PROPERTY_AUTO(time_t,online_time); //上限时刻

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

    PROPERTY_AUTO(bool,update_flag);


public:
    caster_node()
    {
        UID("");
        node_name("");
        set_version("");
        tag_version("");
        run_platform("");

        cpu_usage(0.0);
        mem_usage(0);
        queue_delay(0);
        sub_ping_delay(0);
        sub_tcp_delay(0);
        pub_ping_delay(0);
        pub_tcp_delay(0);
        send_total(0);
        send_speed(0);
        recv_total(0);
        recv_speed(0);

        connnect_count(0);
        server_count(0);
        client_count(0);

        online_time(0);

        update_time(0);

        update_flag(false);
    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["node_name"] = node_name();
        info["set_version"] = set_version();
        info["tag_version"] = tag_version();
        info["run_platform"] = run_platform();

        info["cpu_usage"] = cpu_usage();
        info["mem_usage"] = mem_usage();
        info["queue_delay"] = queue_delay();
        info["sub_ping_delay"] = sub_ping_delay();
        info["sub_tcp_delay"] = sub_tcp_delay();
        info["pub_ping_delay"] = pub_ping_delay();
        info["pub_tcp_delay"] = pub_tcp_delay();
        info["send_total"] = send_total();
        info["send_speed"] = send_speed();
        info["recv_total"] = recv_total();
        info["recv_speed"] = recv_speed();


        info["connnect_count"] = connnect_count();
        info["server_count"] = server_count();
        info["client_count"] = client_count();


        info["online_time"] = online_time();

        info["update_time"] = update_time();
        info["update_flag"] = update_flag();

        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        node_name(info, "node_name");
        set_version(info, "set_version");
        tag_version(info, "tag_version");
        run_platform(info, "run_platform");

        cpu_usage(info, "cpu_usage");
        mem_usage(info, "mem_usage");
        queue_delay(info, "queue_delay");
        sub_ping_delay(info, "sub_ping_delay");
        sub_tcp_delay(info, "sub_tcp_delay");
        pub_ping_delay(info, "pub_ping_delay");
        pub_tcp_delay(info, "pub_tcp_delay");
        send_total(info, "send_total");
        send_speed(info, "send_speed");
        recv_total(info, "recv_total");
        recv_speed(info, "recv_speed");

        connnect_count(info, "connnect_count");
        server_count(info, "server_count");
        client_count(info, "client_count");


        online_time(info, "online_time");

        update_time(info, "update_time");

        return 0;
    }

};
