#pragma once
#include <event2/util.h>
#include <QObject>
#include <QtQml/qqml.h>
#include <QRandomGenerator>
#include "EventWorker.h"
#include "EventOperationBase.h"
#include "stdafx.h"
#include "CasterMonitor.h"


/*
 *      创建一个对象
 *      设置属性
 *      绑定回调
 *      执行操作
 */

class EventAddAccount : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY_AUTO(QVariantMap, account_info)
public:
    explicit EventAddAccount(QObject *parent = nullptr): RedisOperationBase(parent) {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override {

        auto UID= m_account_info["UID"].toString();
        auto info= JsonToQString(variantMapToJson(m_account_info)) ;

        redisAsyncCommand(ctx, Redis_Add_Account_Callback, this, "HSETNX ACT:ACCOUNT %s %s",UID.toStdString().c_str(),info.toStdString().c_str());
    }


    void active_account(redisAsyncContext *ctx)
    {

        // 记录的类型
        //  field   账号
        //  vailue  账号状态（0未激活,1已激活） 账号类型(0永久,1期限,2时限),账号激活(1)，账号剩余激活时长(2), 账号最晚失效时间（2.3）,账号权限组     TTL

        //  永久账号                 1              0                     0                0               0              ;;;          不过期
        //  已激活的期限账号          1              1                     0                0           1975224421         ;;;         1975224421
        //  未激活的期限账号          0              1                     30               0           2000000000         ;;;         2000000000
        //  延迟激活期限账号          1              1                     30               0           2000000000         ;;;         1970000000（第一次登录后刷新过期时间）
        //  已激活的时限账号          1              2                     0             86400          2000000000         ;;;         2000000000


        user_account item;
        item.setInfo(variantMapToJson(m_account_info));

        json info;
        info["account"]=item.account();
        info["password"]=item.password();
        info["active"]=item.time_active()==0? false:true;
        info["type"]=item.type();
        info["date_limit"]=item.time_valid();       // 有效天数      0:不限制
        info["time_limit"]=item.time_limit();       // 在线时长限制   0：不限制
        info["access"]=item.access();               // 准入类型
        info["connect_limit"]=item.access_limit();  // 连接数限制    0：不限制
        info["group"]=item.access_group();          // 访问组        "ALL"：不限制
        info["expire"]=item.time_expired();         // 过期时间      0:"不限制"

        // 判断账号是否是启用状态，如果是启用状态，那么把这个账号添加到ACT:ACTIVE中去
        if(item.state()==0)
        {
            return ; //未启用账号，不需要添加
        }

        // 判断账号类型，永久账号，ACT:ACCOUNT不设置过期时间
        if(item.type()==0)
        {
            //  //
            redisAsyncCommand(ctx, Redis_Active_Account_Callback, this, "HSETNX ACT:ACTIVE %s %s",
                              item.account().c_str(),
                              info.dump().c_str()
                              );

        }
        else  // 如果是非永久账号（期限，时限），必须要有过期时间，那么给ACT:ACTIVE设置有效期到指定时间
        {
            redisAsyncCommand(ctx, Redis_Active_Account_Callback, this, "HSETEX ACT:ACTIVE EXAT %s FIELDS 1 %s %s",
                              std::to_string(item.time_expired()).c_str(),
                              item.account().c_str(),
                              info.dump().c_str()
                              );
        }


        // 如果是未激活的账号，那么激活的时候还要更新一下过期时间，以及账号的激活日期
        // 只针对active=0的情况

        // 除了更新active表之外，还要更新账号表

        // 如果是时限账号
        // 有一个在线时长记录表


    }

public:

    static void Redis_Add_Account_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventAddAccount *>(privdata);


        QVariantMap info;
        info["type"]="ADD";

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对

            Q_EMIT svr->operateFinished(svr->id(),false,info);
        }

        if(reply->integer==1)
        {
            //添加成功
            Q_EMIT svr->operateFinished(svr->id(),true,info);

            // 根据账号配置项来激活账号
            svr->active_account(c);
        }
        else
        {
            //添加失败
            Q_EMIT svr->operateFinished(svr->id(),false,info);
        }


    }

    static void Redis_Active_Account_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventAddAccount *>(privdata);

        QVariantMap info;
        info["type"]="ACTIVE";

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->id(),false,info);
        }

        if(reply->integer==1)
        {
            //添加成功
            Q_EMIT svr->operateFinished(svr->id(),true,info);

        }
        else
        {
            //添加失败
            Q_EMIT svr->operateFinished(svr->id(),false,info);
        }


    }

};


class EventSetAccount : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY_AUTO(QVariantMap, account_info)
public:
    explicit EventSetAccount(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


class EventGetAccount : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY_AUTO(QVariantMap, account_info)
public:
    explicit EventGetAccount(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }

    void execute(redisAsyncContext *ctx) override {
        // Q_UNUSED(base);

        // 添加一条记录


        // 调用Monitor的信号


    }

public:


public:

};


class EventDelAccount : public RedisOperationBase
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY_AUTO(QVariantMap, account_info)
public:
    explicit EventDelAccount(): RedisOperationBase() {};

    Q_INVOKABLE QString name() const override { return typeid(this).name(); }\

        void execute(redisAsyncContext *ctx) override {
        auto UID= m_account_info["UID"].toString();

        redisAsyncCommand(ctx, NULL, NULL, "HDEL ACT:ACTIVE %s",UID.toStdString().c_str());
        redisAsyncCommand(ctx, Redis_Del_Account_Callback, this, "HDEL ACT:ACCOUNT %s",UID.toStdString().c_str());
    }

public:


    static void Redis_Del_Account_Callback(redisAsyncContext *c, void *r, void *privdata)
    {
        // 解析数据
        auto reply = static_cast<redisReply *>(r);
        auto svr = static_cast<EventDelAccount *>(privdata);

        if (!reply)
        {
            return;
        }
        if (reply->type != REDIS_REPLY_INTEGER)
        {
            // 回应不对
            Q_EMIT svr->operateFinished(svr->id(),false,QVariantMap());
        }

        if(reply->integer==1)
        {
            //添加成功
            Q_EMIT svr->operateFinished(svr->id(),true,QVariantMap());
        }
        else
        {
            //添加失败
            Q_EMIT svr->operateFinished(svr->id(),false,QVariantMap());
        }

    }

};
