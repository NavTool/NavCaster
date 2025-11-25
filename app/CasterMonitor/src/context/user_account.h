#pragma once
#include "util.h"



/*
 *  账号激活机制
 *  设置一个注册时间  这条记录添加的时间
 *  设置一个激活时间
 *  设置一个天数有效期（可以使用的天数）
 *  设置一个时长有效期（可以使用的秒数）
 *  设置一个过期时间
 *
 *  不同的账号类型有不同的过期判断方法
 *
 *  对于永久账号，不需要判断，直接返回有效（在ACTIVE表中不过期）
 *
 *  对于期限账号，也不需要判断，因为如果过期，那么就不会在ACTIVE表中
 *
 *  对于注册激活账号（在ACTIVE表中不过期/设置最晚过期时间），在第一次登录完成后，转换成期限账号(更新过期时间）
 *
 *  对于时限账号，（在ACTIVE表中不过期/设置最晚过期时间）在在线时长超过限制后，从ACTIVE表中删除
 *
 *  判断过期时间，超过这个时间，则账号过期不能登录
 *  判断在线秒数，在线秒数超过设置值，账号不能登录
 *
 *  登录流程
 *
 *  1、判断是否存在该账号(在ACT:ACTIVE池子中）,账号不存在则登录失败（可能是未启用，可能是被禁用，或者过期），反正不在这个表里的都不可用
 *
 *  2、添加登录记录（这个时候才开始真正的使用这个账号，如果是未激活，那么要更新账号状态为激活状态，同时根据有效时间
 *
 *  3、查询账号是否停用
 *
 *  3、查询账号
 *
 *
 */










class user_account{

private:
    PROPERTY_AUTO(std::string, UID);      // 账户名  如果勾选了Ntrip1.0的基站，那么UID会是密码，其他情况下，UID是用户名
    PROPERTY_AUTO(std::string, account);  // 用户名
    PROPERTY_AUTO(std::string, passowrd); // 密码

    PROPERTY_AUTO(int, type);             /* 账号类型
                             * 0：未知
                             * 1：期限账号
                             * 2：永久账号
                             * 4：时限账号（在线时长）
                             */

    PROPERTY_AUTO(int, state);            /* 账号状态  只有两个状态  停用和启用，配合判断别的状态来得知账号的状态
                             * 0：已停用
                             * 1：已启用
                             *  ：未激活（已经启用，但是没有激活时间）
                             *  ：已激活（已经启用，有激活时间）
                             *  ：已过期（已经启用，但是已经过期）
                             * （已删除）
                             */

    PROPERTY_AUTO(std::string, access_group);     /* 访问权限（账号可以访问的数据分组）  按照分号分隔
                            *   ALL          不进行访问权限判断
                            *   GROUP_NAME   查询这个分组里是否包含数据
                            */

    // 对于期限账号（有非激活和已经激活两种状态）
    // 对于时限账号（在注册的时候就要写入激活时间，只会有已激活一个状态）

    // 期限账号才有用
    PROPERTY_AUTO(time_t, valid_time);     // 有效时间
    // 时限账号才有用
    PROPERTY_AUTO(time_t, online_limit);   // 在线时长限制

    // 都会用到(如果没有设置激活时间，那么设置激活时间，同时更新过期时间)
    PROPERTY_AUTO(time_t, active_time);     // 激活时间
    PROPERTY_AUTO(time_t, expired_time);    // 过期时间
    PROPERTY_AUTO(time_t, register_time);   // 注册时间

    PROPERTY_AUTO(int, access_limit);       //允许连接数量

    PROPERTY_AUTO(std::string, userName);           // 用户名/所属机构
    PROPERTY_AUTO(std::string, contactPerson);      // 联系人
    PROPERTY_AUTO(std::string, contactInfo);        // 联系方式

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
        access_group(0);

        register_time(0);
        active_time(0);
        expired_time(0);

        access_limit(0);

        userName("");
        contactPerson("");
        contactInfo("");

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
        info["access_group"] = access_group();

        info["register_time"] = register_time();
        info["active_time"] = active_time();
        info["expired_time"] = expired_time();

        info["access_limit"] = access_limit();

        info["userName"] = userName();
        info["contactPerson"] = contactPerson();
        info["contactInfo"] = contactInfo();

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
        access_group(info, "access_group");

        register_time(info, "register_time");
        active_time(info, "active_time");
        expired_time(info, "expired_time");

        access_limit(info, "access_limit");

        userName(info, "userName");
        contactPerson(info, "contactPerson");
        contactInfo(info, "contactInfo");

        modifiedDate(info, "modifiedDate");

        return 0;
    }

};
