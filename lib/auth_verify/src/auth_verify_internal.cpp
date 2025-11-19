#include "auth_verify_internal.h"

#include <spdlog/spdlog.h>
#include "knt.h"

auth_internal::auth_internal(/* args */)
{
}

auth_internal::~auth_internal()
{
}

auth_internal *auth_internal::getInstance()
{
    static auth_internal *instance = new auth_internal();
    return instance;
}

int auth_internal::init(json conf, event_base *base)
{

    _redis_IP = conf["Redis_IP"];
    _redis_port = conf["Redis_Port"];
    _redis_Requirepass = conf["Redis_Requirepass"];

    _base = base;
    return 0;
}

int auth_internal::start()
{
    pubAttemptReconnect();
    subAttemptReconnect();

    _timeout_tv.tv_sec = _update_intv;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);
    event_add(_timeout_ev, &_timeout_tv);

    return 0;
}

int auth_internal::stop()
{
    redisAsyncDisconnect(_sub_context);
    redisAsyncFree(_sub_context);
    redisAsyncDisconnect(_pub_context);
    redisAsyncFree(_pub_context);
    return 0;
}

int auth_internal::verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type)
{
    auto ctx = new auth_ctx;
    ctx->type = type;
    ctx->user_name = user_name;
    ctx->user_pwd = user_pwd;
    ctx->cb = cb;
    ctx->arg = arg;

    // 如果是匿名模式，那么账户系统就完全失效，只会生效ACT:UNNAMED
    if (type == AuthType::SERVER && _anonymous_server_login) // 基站匿名登录
    {
        // 自动注册一个匿名账户
        redisAsyncCommand(_pub_context, Redis_Add_Unnamed_Callback, ctx, "HSET ACT:UNNAMED %s %s", user_name, util_get_time_stamp_str().c_str());
    }
    else if (type == AuthType::CLIENT && _anonymous_client_login) // 用户匿名登录
    {
        // 自动注册一个匿名账户
        redisAsyncCommand(_pub_context, Redis_Add_Unnamed_Callback, ctx, "HSET ACT:UNNAMED %s %s", user_name, util_get_time_stamp_str().c_str());
    }
    else
    {
        // 查询这个账户的相关信息，等待回调
        redisAsyncCommand(_pub_context, Redis_Verify_Callback, ctx, "HGET ACT:ACTIVE %s", user_name);
    }
    return 0;
}

int auth_internal::add_login_record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type)
{
    // 查询要注册的频道
    auto find = _register_map.find(user_name);
    if (find == _register_map.end())
    {
        // 还没有该频道的注册记录
        std::unordered_map<std::string, auth_cb_item> channel_cbs;
        _register_map.insert(std::pair<std::string, std::unordered_map<std::string, auth_cb_item>>(user_name, channel_cbs));
    }
    find = _register_map.find(user_name);

    try
    {
        // 创建一条新的stream记录
        auth_status item(user_name, connect_key);
        _register_status_map.insert(std::pair<std::string, auth_status>(connect_key, item));
        // 向云端插入记录
        // if (_upload_base_stat)
        // {
        // redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX ACT:STAT EX %s FIELDS 1 %s %s", std::to_string(_key_expire_time).c_str(), connect_key, item.get_status_str().c_str());
        // }

        // 将cb注册回调记录到本地
        auth_cb_item cb_item;
        cb_item.connect_key = connect_key;
        // cb_item.channel = channel;
        cb_item.user_name = user_name;
        cb_item.cb = cb;
        cb_item.arg = arg;

        if (find->second.find(connect_key) != find->second.end())
        {
            throw std::invalid_argument("Connect_Key is already in the register map");
        }
        find->second.insert(std::pair<std::string, auth_cb_item>(connect_key, cb_item));

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性
        // 即：查询到的结果已经包含当前记录，因此避免查询-插入后还需要再进行一步检测的步骤
        // 问题：如果两个节点同时插入了记录，同时查询到记录，按照规则，可能都会被踢掉？
        //       但是踢掉也只是通过广播的形式来踢掉，没啥影响，大不了都登不上，都下线一次
        // 反正记录只能由注册者自己删除（或者说注册该连接的Caster维护）

        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX ACT:REC:%s EX %s FIELDS 1 %s %s",
                          user_name,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理

        auto ctx = new auth_ctx();
        ctx->type = type;
        ctx->user_name = user_name;
        // ctx->user_pwd = user_pwd;
        ctx->connect_key = connect_key;
        ctx->arg = arg;
        ctx->cb = cb;

        redisAsyncCommand(_pub_context, Redis_Add_Login_Callback, ctx, "HGETALL ACT:REC:%s", user_name); // 查询当前频道的所有记录
    }
    catch (std::exception &e)
    {
        // 发生异常，此次插入失败
        spdlog::warn("[{}:{}]: exception:  {}", __class__, __func__, e.what());
        return 1;
    }
    return 0;
}

int auth_internal::add_logout_record(const char *user_name, const char *connect_key, AuthType type)
{
    // 从该频道的HASH Map中删除指定Connect_Key记录
    // 删除成功
    // 从本地注册回调Map中删除指定的记录

    // 查询要注册的频道
    auto user_registers = _register_map.find(user_name);
    if (user_registers == _register_map.end())
    {
        // 错误：没有该频道的注册记录
        return 1;
    }
    auto item = user_registers->second.find(connect_key);
    if (item == user_registers->second.end())
    {
        // 错误：没有该连接的注册记录
        return 2;
    }

    // 删除该条记录
    user_registers->second.erase(item);
    // 删除Redis记录
    redisAsyncCommand(_pub_context, NULL, NULL, "HDEL ACT:REC:%s %s", user_name, connect_key);

    // 删除status记录
    auto str = _register_status_map.find(connect_key);
    if (str == _register_status_map.end())
    {
        // 错误，找不到这条stream状态记录仪
        return 3;
    }
    _register_status_map.erase(connect_key);

    return 0;
}

int auth_internal::init_sub_context()
{
    redisAsyncCommand(_sub_context, Redis_Broadcast_Callback, this, "SUBSCRIBE AUTH:BROADCAST");
    return 0;
}

int auth_internal::init_pub_context()
{
    redisAsyncCommand(_pub_context, NULL, NULL, "DEL MPT:STAT");
    redisAsyncCommand(_pub_context, NULL, NULL, "DEL USR:STAT");
    return 0;
}

int auth_internal::subAttemptReconnect()
{
    if (_is_sub_connected)
    {
        return 0;
    }

    // 初始化redis连接
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, _redis_IP.c_str(), _redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 10;
    options.connect_timeout = &tv;

    _sub_context = redisAsyncConnectWithOptions(&options);
    if (_sub_context->err)
    {
        /* Let *c leak for now... */
        spdlog::error("redis eror: {}", _sub_context->errstr);
        redisAsyncFree(_sub_context);
        _pub_context = nullptr;
        // 直接退出程序
        exit(1);
    }
    _sub_context->data = this;

    redisLibeventAttach(_sub_context, _base);
    redisAsyncSetConnectCallback(_sub_context, Redis_Sub_Connect_Cb);
    redisAsyncSetDisconnectCallback(_sub_context, Redis_Sub_Disconnect_Cb);

    redisAsyncCommand(_sub_context, NULL, NULL, "AUTH %s", _redis_Requirepass.c_str());

    return 0;
}

int auth_internal::pubAttemptReconnect()
{
    if (_is_pub_connected)
    {
        return 0;
    }

    // 初始化redis连接
    redisOptions options = {0};
    REDIS_OPTIONS_SET_TCP(&options, _redis_IP.c_str(), _redis_port);
    struct timeval tv = {0};
    tv.tv_sec = 10;
    options.connect_timeout = &tv;

    _pub_context = redisAsyncConnectWithOptions(&options);
    if (_pub_context->err)
    {
        /* Let *c leak for now... */
        spdlog::error("redis eror: {}", _pub_context->errstr);
        redisAsyncFree(_pub_context);
        _pub_context = nullptr;
        // 直接退出程序
        exit(1);
    }
    _pub_context->data = this;

    redisLibeventAttach(_pub_context, _base);
    redisAsyncSetConnectCallback(_pub_context, Redis_Pub_Connect_Cb);
    redisAsyncSetDisconnectCallback(_pub_context, Redis_Pub_Disconnect_Cb);

    redisAsyncCommand(_pub_context, NULL, NULL, "AUTH %s", _redis_Requirepass.c_str());

    return 0;
}

int auth_internal::upload_record_item()
{
    for (auto iter : _register_map)
    {
        for (auto items : iter.second)
        {
            // 更新注册用户
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE ACT:REC:%s %s FIELDS 1 %s", items.second.user_name.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }
    for (auto iter : _unnamed_map)
    {
        for (auto items : iter.second)
        {
            // 更新基站注册连接有效期
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE ACT:REC:%s %s FIELDS 1 %s", items.second.user_name.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    return 0;
}

void auth_internal::Redis_Pub_Connect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<auth_internal *>(c->data);

    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
        svr->_is_pub_connected = true;
        svr->init_pub_context();
    }
    else
    {
        svr->_is_pub_connected = false;
        svr->_pub_context_errstr = c->err;
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_pub_context_errstr);
        svr->_pub_context = nullptr; /* avoid stale pointer when callback returns */

        exit(1);
    }
    svr->pubAttemptReconnect();
}

void auth_internal::Redis_Sub_Connect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<auth_internal *>(c->data);

    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
        svr->_is_sub_connected = true;
        svr->init_sub_context();
    }
    else
    {
        svr->_is_sub_connected = false;
        svr->_sub_context_errstr = c->err;
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_sub_context_errstr);
        svr->_sub_context = nullptr; /* avoid stale pointer when callback returns */

        exit(1);
    }
    svr->subAttemptReconnect();
}

void auth_internal::Redis_Pub_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<auth_internal *>(c->data);

    svr->_is_pub_connected = false;
    svr->_pub_context_errstr = c->err;
    svr->_pub_context = NULL; /* avoid stale pointer when callback returns */
    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: redis info: {}", __class__, __func__, svr->_pub_context_errstr);
    }
    else
    {
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_pub_context_errstr);
        svr->pubAttemptReconnect();
    }
}

void auth_internal::Redis_Sub_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<auth_internal *>(c->data);

    svr->_is_sub_connected = false;
    svr->_sub_context_errstr = c->err;
    svr->_sub_context = NULL; /* avoid stale pointer when callback returns */
    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: redis info: {}", __class__, __func__, svr->_sub_context_errstr);
    }
    else
    {
        spdlog::error("[{}:{}]: redis eror: {}", __class__, __func__, svr->_sub_context_errstr);
        svr->subAttemptReconnect();
    }
}

void auth_internal::Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata)
{
}

void auth_internal::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<auth_internal *>(arg);

    // 本地维护的在线用户续期
    svr->upload_record_item();
}

void auth_internal::Redis_Add_Unnamed_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    // 判断是否添加成功

    // 本地添加登录限制记录（用户数量）

    auth_limit limit;
    limit._online_limit = 9999;

    auth_internal::getInstance()->_unnamed_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, limit));

    AuthReply Reply;
    Reply.type = AUTH_REPLY_OK;
    ctx->cb(nullptr, ctx->arg, &Reply);

    delete ctx;
}

void auth_internal::Redis_Verify_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    // 判断是否添加成功

    // 本地添加登录限制记录（用户数量）

    auth_limit limit;
    limit._online_limit = 9999;

    auth_internal::getInstance()->_register_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, limit));
    // 解析查询到的信息,存储到本地


    // 如果不存在，那么就不允许登录
    AuthReply Reply;
    Reply.type = AUTH_REPLY_OK; // AUTH_REPLY_ERR;
    ctx->cb(nullptr, ctx->arg, &Reply);

    delete ctx;
}

void auth_internal::Redis_Add_Login_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto arg = static_cast<std::pair<auth_internal *, auth_cb_item> *>(privdata);
    auto svr = arg->first;
    auto cb_item = arg->second;

    bool _check = false; // 检验记录中是否包含本条记录

    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::list<std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        if (strcmp(field, cb_item.connect_key.c_str()) == 0)
        {
            _check = true;
        }
        records.push_back(field);
    }

    // try
    // {
    //     if (!_check)
    //     {
    //         throw std::logic_error("Can't Find Recored"); // 在返回的记录中不包含本条记录
    //     }

    //     //_check通过的话，records.size()必定大于等于1
    //     if (records.size() != 1 && !svr->_base_enable_mult) // 有多个连接记录且设置不允许多个记录
    //     {
    //         if (svr->_base_keep_early) // 已在线的优先级高，踢出当前
    //         {
    //             throw std::logic_error("Base already online ,don't allow base duplicate logins");
    //         }

    //         for (auto iter : records) // 新上线的优先级高，踢出出其他已在线记录
    //         {
    //             if (iter != cb_item.connect_key)
    //             {
    //                 svr->send_status_base_channel(cb_item.channel.c_str(), iter.c_str(), CasterReply::ERR, "New same name Base Login, kick out this Connect!");
    //             }
    //         }
    //     }
    //     // else 有1个或者多个连接，但是允许多个记录
    //     // 正常，返回一个成功回调
    //     catser_reply Reply;
    //     Reply.type = CasterReply::OK;
    //     Reply.str = "";
    //     cb_item.cb(NULL, cb_item.arg, &Reply);
    // }
    // catch (const std::exception &e)
    // {
    //     // std::cerr << e.what() << '\n';
    //     // 异常，发送关闭当前连接的请求
    //     svr->send_status_base_channel(cb_item.channel.c_str(), cb_item.connect_key.c_str(), CasterReply::ERR, e.what());
    // }

    delete arg;

    // AuthReply reply;
    // reply.type = AUTH_REPLY_OK;
    // cb(nullptr, arg, &reply);
    // return 0;
}

void auth_internal::Redis_Add_Logout_Callback(redisAsyncContext *c, void *r, void *privdata)
{
}

void auth_internal::Redis_Update_Active_Callback(redisAsyncContext *c, void *r, void *privdata)
{
}

std::string auth_status::get_status_str()
{
    json info;

    info["UID"] = _UID;
    info["type"] = _type;
    info["account"] = _account;
    info["ip"] = _ip;

    info["ecef_x"] = _ecef_x;
    info["ecef_y"] = _ecef_y;
    info["ecef_z"] = _ecef_z;
    info["first_time"] = _first_time;

    info["last_time"] = _last_time;

    return info.dump();
}
