#pragma once
#include "knt.h"
#include "util.h"

class alias_rule{

private:
    PROPERTY_AUTO(std::string,UID);        // TCP连接唯一标识
    PROPERTY_AUTO(std::string,alias_mpt);  // 接入的挂载点

    PROPERTY_AUTO(int, state);            /* 账号状态  只有两个状态  停用和启用，配合判断别的状态来得知账号的状态
                             * 0：已停用
                             * 1：已启用
*/


    // 挂载点名称，支持

    PROPERTY_AUTO(bool,update_flag);

public:


    std::map<int,std::string> _maping_list; // 编号（优先级）  挂载点名

    alias_rule()
    {
        UID("");
        alias_mpt("");
        state(0);

        update_flag(false);
    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["alias_mpt"] = alias_mpt();
        info["state"] = state();

        info["update_flag"] = update_flag();

        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        alias_mpt(info, "alias_mpt");
        state(info, "state");

        return 0;
    }

};
