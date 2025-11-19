#define __class__ "redis_msg_internal"
#include "Auth_Verify.h"
#include <string>
#include <set>
#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// 内部有一个定时器, 用来维护所有在线用户的有效期
// 当用户添加注册记录之后, 查询用户具体记录, 更新激活时间, 有效期, 同时添加一个定时器, 定时器时长为剩余有效时间
// 当超时函数触发之后, 触发用户踢出回调

// 用户的账号查询逻辑, 传入用户名和密码, 检测用户名密码是否正确, 账号是否是启用, 失效时间是否小于当前时间（已过期）
// 如果账号已经过期, 是否要移动到其他表格中？

/*
    多节点用户数据同步


    用户登录, 传递用户名和密码

    从数据库查询用户的信息, 比对账户名和密码

    验证通过, 返回登录数量信息, 过期信息, 账户分组信息ALL/SH(未分组默认给全部的访问权限,分组后只能访问分组后的挂载点)

    ------进入第二步

    用户登录成功, 添加登录记录

    向数据库中添加登录记录, 查询登录数量是否已经超过上限

    如果超过上限, 根据登录策略, 选择踢出最早用户, 当前用户

    返回登录成功

    ----- 第三步

    用户取消登录记录, 从数据库中删除该记录


    定期函数（只查询本地已经注册的账户）, 查询当前用户是否合法（是否已经过期, 账号是否已经非激活, 数量是否超过上限）, 根据判断结果选择是否踢出用户
    上报当前的状态信息


    ACT:ACTIVE                 [账号名-账号验证信息[账户，密码，有效日期，最大数量，分组信息]]            当前处于激活状态的账户密码（自动过期）
    ACT:ACCOUNT                [账号名-账号信息]        所有注册账号的信息                        所有已经注册的账户信息（不过期）   注册账号的时候，如果是激活状态，那就向ACTIVE中写入一条记录，如果是非激活状态，那就删除掉
    ACT:UNNAMED                                                                                匿名账户
    ACT:REC:KORO5              账号名-[连接的ConnectKey-添加记录时间]  （每条的有有效期是固定的）

*/

struct auth_ctx
{
    AuthType type=AuthType::UNKNOWN;
    std::string user_name;
    std::string user_pwd;
    std::string connect_key;
    VerifyCallback cb=nullptr;
    void *arg=nullptr;
};


class auth_status
{
private:
    // 用户名
    std::string _UID; // TCP连接唯一标识

    int _type = 0;
    // 用户名
    std::string _account;
    // 登录的IP
    std::string _ip;
    // 用户上线时刻
    std::time_t _first_time = 0; // 上线时刻

    // 最后下线时刻（最后更新时刻）
    std::time_t _last_time = 0.0;

    // 用户坐标信息（如果有）
    double _ecef_x = 0.0;
    double _ecef_y = 0.0;
    double _ecef_z = 0.0;

public:
    auth_status(std::string user_name, std::string connect_key) {};

    std::string get_status_str();
};

class auth_limit
{
public:
    int _online_limit = 0;

};

class auth_cb_item
{
public:
    std::string connect_key;
    // std::string channel;
    std::string user_name;
    VerifyCallback cb;
    void *arg;
};

class auth_internal
{
private:
    std::string _redis_IP;
    int _redis_port;
    std::string _redis_Requirepass;

    event_base *_base;

    event *_timeout_ev;
    timeval _timeout_tv;

    // conf
    int _unactive_time = 10; // 站点更新时间和当前时间差距多少秒会被认为已挂掉
    int _update_intv = 1;
    int _key_expire_time = 3600; // Hash键值默认续期时间

    bool _anonymous_server_login = true;
    bool _anonymous_client_login = true;

private:
    // 本地已经注册的用户
    // 频道名(挂载点, 用户名)：[具体连接key:注册回调]
    std::unordered_map<std::string, std::unordered_map<std::string, auth_cb_item>> _register_map;
    std::unordered_map<std::string, std::unordered_map<std::string, auth_cb_item>> _unnamed_map;

    std::unordered_map<std::string, auth_limit> _register_limit_map;
    std::unordered_map<std::string, auth_limit> _unnamed_limit_map;

    std::unordered_map<std::string, auth_status> _register_status_map; // connect_key/str_status  //移动站的状态统计信息

public:
    bool _is_pub_connected = false;
    bool _is_sub_connected = false;
    int _pub_reconnect_count = 0; // 重连计数  连接成功后归零   重连失败后, 等待时间0、2、4、8、10(max)
    int _sub_reconnect_count = 0; // 重连计数
    std::string _pub_context_errstr;
    std::string _sub_context_errstr;
    redisAsyncContext *_pub_context = nullptr;
    redisAsyncContext *_sub_context = nullptr;

public:
    auth_internal(/* args */);
    ~auth_internal();

    // 返回单例实例
    static auth_internal *getInstance();

    int init(json conf, event_base *base);

    int start();
    int stop();

    int verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type);

    int add_login_record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type);

    int add_logout_record(const char *user_name, const char *connect_key, AuthType type);

private:
    int init_sub_context();
    int init_pub_context();

    int subAttemptReconnect();
    int pubAttemptReconnect();

    int upload_record_item(); // 将本地记录的所有连接、挂载点和用户更新到redis中(更新记录时间)

public:
    static void Redis_Pub_Connect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Sub_Connect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Pub_Disconnect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Sub_Disconnect_Cb(const redisAsyncContext *c, int status);

    // 广播频道的回调
    static void Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata);

    // libevent 回调
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    // 查询用户信息的回调
    static void Redis_Verify_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 添加匿名账户的回调
    static void Redis_Add_Unnamed_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 添加登录信息的回调
    static void Redis_Add_Login_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 移除登录信息的回调
    static void Redis_Add_Logout_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 获取激活用户信息状态的回调
    static void Redis_Update_Active_Callback(redisAsyncContext *c, void *r, void *privdata);
};
