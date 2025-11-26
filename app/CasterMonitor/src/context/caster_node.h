#pragma once
#include "util.h"


class caster_node{

private:
    PROPERTY_AUTO(std::string, UID);      // 账户名  如果勾选了Ntrip1.0的基站，那么UID会是密码，其他情况下，UID是用户名

    PROPERTY_AUTO(std::string,set_version);
    PROPERTY_AUTO(std::string,tag_version);
    PROPERTY_AUTO(int,connnect_count);
    PROPERTY_AUTO(int,server_count);
    PROPERTY_AUTO(int,client_count);
    PROPERTY_AUTO(double,cpu_usage);
    PROPERTY_AUTO(int,mem_usage);
    PROPERTY_AUTO(time_t,online_time);

    PROPERTY_AUTO(time_t, update_time); // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

    PROPERTY_AUTO(bool,update_flag);


public:
    caster_node()
    {
        UID("");

        set_version("");
        tag_version("");
        connnect_count(0);
        server_count(0);
        client_count(0);
        cpu_usage(0.0);
        mem_usage(0);
        online_time(0);

        update_time(0);
        update_flag(0);
    }

    json info()
    {
        json info;
        info["UID"] = UID();

        info["set_version"] = set_version();
        info["tag_version"] = tag_version();

        info["connnect_count"] = connnect_count();
        info["server_count"] = server_count();
        info["client_count"] = client_count();

        info["cpu_usage"] = cpu_usage();
        info["mem_usage"] = mem_usage();
        info["online_time"] = online_time();

        info["update_time"] = update_time();
        info["update_flag"] = update_flag();

        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        set_version(info, "set_version");
        tag_version(info, "tag_version");

        connnect_count(info, "connnect_count");
        server_count(info, "server_count");
        client_count(info, "client_count");

        cpu_usage(info, "cpu_usage");
        mem_usage(info, "mem_usage");
        online_time(info, "online_time");

        update_time(info, "update_time");

        return 0;
    }

};
