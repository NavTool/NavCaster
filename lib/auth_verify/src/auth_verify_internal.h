#define __class__ "redis_msg_internal"

#include <string>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

//内部有一个定时器，用来维护所有在线用户的有效期
//当用户添加注册记录之后，查询用户具体记录，更新激活时间，有效期，同时添加一个定时器，定时器时长为剩余有效时间
//当超时函数触发之后，触发用户踢出回调


//用户的账号查询逻辑，传入用户名和密码，检测用户名密码是否正确，账号是否是启用，失效时间是否小于当前时间（已过期）
//如果账号已经过期，是否要移动到其他表格中？

 
struct user_info
{

};

class redis_auth_internal
{
private:
    /* data */
public:
    redis_auth_internal(/* args */);
    ~redis_auth_internal();
};


