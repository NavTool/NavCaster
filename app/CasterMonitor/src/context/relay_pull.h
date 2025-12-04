#pragma once
#include "knt.h"
#include "util.h"

class relay_pull{

private:
    PROPERTY_AUTO(std::string,UID);        // TCP连接唯一标识
    PROPERTY_AUTO(std::string,login_mpt);  // 接入的挂载点

    PROPERTY_AUTO(int, type);              /* 接入类型
                             *  0：未知
                             *  1：Ntip Client 1.0
                             *  2：Ntip Client 2.0
                             *  3：Tcp Client
                             *  4：Tcp Server
                             */

    PROPERTY_AUTO(std::string,target_ip);   // 目标IP
    PROPERTY_AUTO(int, target_port);        // 目标端口
    PROPERTY_AUTO(std::string,target_mpt);     // 使用用户名
    PROPERTY_AUTO(std::string,target_account);     // 使用用户名
    PROPERTY_AUTO(std::string,target_password);    // 使用密码

    // 本地接入挂载点

    // 任务执行状态

    PROPERTY_AUTO(bool,update_flag);

public:
    relay_pull()
    {
        UID("");
        login_mpt("");

        type(0);

        target_ip("");
        target_port(0);
        target_mpt("");
        target_account("");
        target_password("");

        update_flag(false);
    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["login_mpt"] = login_mpt();

        info["type"] = type();

        info["target_ip"] = target_ip();
        info["target_port"] = target_port();
        info["target_mpt"] = target_mpt();
        info["target_account"] = target_account();
        info["target_password"] = target_password();


        info["update_flag"] = update_flag();

        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        login_mpt(info, "login_mpt");

        type(info, "type");

        target_ip(info, "target_ip");
        target_port(info, "target_port");
        target_mpt(info, "target_mpt");
        target_account(info, "target_account");
        target_password(info, "target_password");

        return 0;
    }

};
