#pragma once
#include "util.h"


class user_account{

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

    PROPERTY_AUTO(bool,update_flag);

public:
    user_account()
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
       update_flag(false);
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


        info["update_flag"] = update_flag();
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
