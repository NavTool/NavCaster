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



//








class user_account{

private:
    PROPERTY_AUTO(std::string, UID);      // 账户名  如果勾选了Ntrip1.0的基站，那么UID会是密码，其他情况下，UID是用户名
    PROPERTY_AUTO(std::string, account);  // 用户名
    PROPERTY_AUTO(std::string, password); // 密码

    PROPERTY_AUTO(int, type);             /* 账号类型
                             * 0：永久
                             * 1：期限账号（日期）
                             * 2：期限账号（天数）
                             * 3：时限账号（在线时长）
                             */

    PROPERTY_AUTO(int, state);            /* 账号状态  只有两个状态  停用和启用，配合判断别的状态来得知账号的状态
                             * 0：已停用
                             * 1：已启用
                             *  ：未激活（已经启用，但是没有激活时间）
                             *  ：已激活（已经启用，有激活时间）
                             *  ：已过期（已经启用，但是已经过期）
                             * （已删除）
                             */
    PROPERTY_AUTO(int, access);/*   账号权限类型
                                *   0 无权限
                                *   1 Ntrip1.0/2.0 Client + Ntrip2.0 Server权限
                                *   2 Ntrip1.0/2.0 Client权限
                                *   3 Ntrip2.0 Server权限
                                *   4 Ntrip1.0 Server权限
                                */
    PROPERTY_AUTO(int, access_limit);       //允许连接数量
    PROPERTY_AUTO(std::string, access_group);     /* 访问权限（账号可以访问的数据分组）  按照分号分隔
                            *   ALL          不进行访问权限判断
                            *   GROUP_NAME   查询这个分组里是否包含数据
                            *   空           没有任何访问权限，基站账号没有访问权限
                            */

    // 对于期限账号（有非激活和已经激活两种状态）
    // 对于时限账号（在注册的时候就要写入激活时间，只会有已激活一个状态）


    // 期限账号才有用
    PROPERTY_AUTO(time_t, time_valid);     // 有效时间
    // 时限账号才有用
    PROPERTY_AUTO(time_t, time_limit);   // 在线时长限制

    // 都会用到(如果没有设置激活时间，那么设置激活时间，同时更新过期时间)
    PROPERTY_AUTO(time_t, time_active);     // 激活时间
    PROPERTY_AUTO(time_t, time_expired);    // 过期时间
    PROPERTY_AUTO(time_t, time_register);   // 注册时间


    PROPERTY_AUTO(std::string, contact_name);           // 用户名/所属机构
    PROPERTY_AUTO(std::string, contact_person);      // 联系人
    PROPERTY_AUTO(std::string, contact_info);        // 联系方式

    PROPERTY_AUTO(time_t, time_modified);    // 记录更新时间

    PROPERTY_AUTO(bool,update_flag);

public:
    user_account()
    {
        UID("");
        account("");
        password("");

        type(0);
        state(0);

        access(0);
        access_limit(0);
        access_group("");

        time_valid(0);
        time_limit(0);
        time_register(0);
        time_active(0);
        time_expired(0);

        contact_name("");
        contact_person("");
        contact_info("");

        time_modified(0);
        update_flag(false);
    }

    json info()
    {
        json info;
        info["UID"] = UID();
        info["account"] = account();
        info["password"] = password();

        info["type"] = type();
        info["state"] = state();

        info["access"] = access();
        info["access_limit"] = access_limit();
        info["access_group"] = access_group();

        info["time_valid"]=time_valid();
        info["time_limit"]=time_limit();
        info["time_register"] = time_register();
        info["time_active"] = time_active();
        info["time_expired"] = time_expired();

        info["contact_name"] = contact_name();
        info["contact_person"] = contact_person();
        info["contact_info"] = contact_info();

        info["time_modified"] = time_modified();

        info["update_flag"] = update_flag();
        return info;
    }

    int setInfo(json info)
    {

        UID(info, "UID");
        account(info, "account");
        password(info, "password");

        type(info, "type");
        state(info, "state");
        access(info,"access");
        access_limit(info, "access_limit");
        access_group(info, "access_group");

        time_valid(info, "time_valid");
        time_limit(info, "time_limit");
        time_register(info, "time_register");
        time_active(info, "time_active");
        time_expired(info, "time_expired");

        contact_name(info, "contact_name");
        contact_person(info, "contact_person");
        contact_info(info, "contact_info");

        time_modified(info, "time_modified");

        return 0;
    }

};
