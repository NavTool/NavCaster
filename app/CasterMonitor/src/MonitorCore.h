#pragma once
#include <string>

class server_info
{
    std::string UID;        // TCP连接唯一标识
    std::string login_mpt;  // 接入的挂载点
    std::string alias_mpt;  // 对外服务的挂载点

    int type=0;             /* 挂载点类型
                             *  0：未知
                             *  1：普通挂载点
                             *  2：最近挂载点
                             *  3：Relay挂载点（Ntrip Client）
                             *  4：Relay挂载点（TCP Client）
                             *  5：Relay挂载点（TCP Server）
                             *  6：Proxy挂载点（Ntrip Client）
                             *  7：Alias挂载点（挂载点添加一个别名，可通过这个别名来获取数据）
                             */
};

class client_info{
    std::string UID;        // TCP连接唯一标识
    std::string login_mpt;  // 接入的挂载点
    std::string inter_mpt;  // 内部提供数据的挂载点（真正使用的挂载点）

    int type=0;             /* 接入类型
                             *  0：未知
                             *  1：普通接入模式
                             *  2：最近基站模式
                             *  3：Proxy模式
                             *
                             */
};

class user_info{
    std::string UID;      // 账户名
    std::string passowrd; // 密码

    int type=0;             /* 账号类型
                             *
                             */

    int state=0;            /* 账号类型
                             *
                             */

    int access=0;           /* 接入类型
                             * 0x00：无权限
                             * 0x01：允许以基站模式接入（Ntrip1.0）（只验证密码）
                             * 0x01：允许以基站模式接入（Ntrip2.0）
                             * 0x02：允许获取实体挂载点数据
                             * 0x04：允许获取最近挂载点数据
                             * 0x08：允许获取Alias挂载点数据（只能获取有限挂载点数据）
                             * 0x10：允许获取Proxy挂载点数据
                             */

    std::string group;
};



class MonitorCore
{
public:
    MonitorCore() {}

    // 返回单例实例
    static MonitorCore *getInstance();



public:

    // 创建线程,连接至Redis

    // 设置定时函数，定期从Redis中获取数据，这个时间可以灵活更改
    int start();





private:











};
