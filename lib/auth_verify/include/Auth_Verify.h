#pragma once
#include <event2/event.h>

// #define AUTH_REPLY_ERR -1
// #define AUTH_REPLY_OK 0

// #define AUTH_REPLY_STRING 1
// #define AUTH_REPLY_ARRAY 2
// #define AUTH_REPLY_INTEGER 3
// #define AUTH_REPLY_NIL 4

enum class AuthReply
{
    ERR = -1,
    OK,
    ACTIVE,
    INACTIVE,
    STRING,
    // ARRAY,
    INTEGER,
    DOUBLE,
    NIL,
};

struct auth_reply
{
    AuthReply type;
    const char *str;
    size_t len;
    int integer = 0;
    double dval = 0.0;
};

enum class AuthBroadcastType
{
    UNKNOWN = 0,
    ACCOUNT_STATUS_UPDATE, // 注册用户状态更新
    ACCOUNT_ACTIVE,        // 注册用户上线
    ACCOUNT_INACTIVE,      // 注册用户下线
    UNNAMED_STATUS_UPDATE, // 匿名用户状态更新
    UNNAMED_ACTIVE,        // 匿名用户上线
    UNNAMED_INACTIVE,      // 匿名用户下线
};

enum class AuthType
{
    UNKNOWN = 0,
    SERVER = 1,
    CLIENT = 2,
    SOURCE = 3
};

typedef void (*VerifyCallback)(const char *request, void *arg, auth_reply *reply);

namespace AUTH
{

    int Init(const char *json_conf, event_base *base);
    int Free();

    // 验证密码是否通过, 返回有效期, 或登录失败(在回调调用前, 回调对象不能被删除)
    int Verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type);

    // 添加登录记录（无论是否成功都会添加一条登录记录）, 登录成功, 添加到在线表, 登录失败, 返回登录失败
    int Add_Login_Record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type);

    // 添加登出记录, 删除在线表记录（后续会在库中添加记录回调的map, 用来踢用户下线？, 所以在下线的时候也要调用一下这个函数, 用来删除绑定回调）
    int Add_Logout_Record(const char *user_name, const char *connect_key, AuthType type);

} // namespace CASTER

// Redis 内部维护表
// 一张
