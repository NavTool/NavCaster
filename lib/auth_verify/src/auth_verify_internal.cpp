#include "auth_verify_internal.h"
#include <list>
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

int auth_internal::init(AuthVerifyOpt opt, event_base *base)
{
    _redis_IP = opt.redis_host();
    _redis_port = opt.redis_port();
    _redis_Requirepass = opt.redis_password();

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
    if ((type == AuthType::SERVER && _anonymous_server_login) || (type == AuthType::CLIENT && _anonymous_client_login))
    {
        // 基站匿名登录 || 用户匿名登录
        // 自动注册一个匿名账户
        redisAsyncCommand(_pub_context, Redis_Add_Temp_Callback, ctx, "HSET ACT:UNNAMED %s %s", user_name, util_get_time_stamp_str().c_str());
    }
    else if (type == AuthType::SOURCE)
    {
        // 数据源登录，不验证密码，直接返回成功
        auth_reply Reply;
        Reply.type = AuthReply::OK;
        Reply.str = "Source Anonymous Login OK";
        Reply.len = strlen(Reply.str);
        cb("", arg, &Reply);
        delete ctx;
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

    if ((type == AuthType::SERVER && _anonymous_server_login) || (type == AuthType::CLIENT && _anonymous_client_login))
    {
        // 基站匿名登录 || 用户匿名登录

        // 先在本地的注册MAP中添加该记录
        auto find = _unnamed_map.find(user_name);
        if (find == _unnamed_map.end())
        {
            // 没有该用户的登录记录，创建一个map
            std::unordered_map<std::string, auth_cb_item> channel_cbs;
            _unnamed_map.insert(std::pair<std::string, std::unordered_map<std::string, auth_cb_item>>(user_name, channel_cbs));
        }
        find = _unnamed_map.find(user_name);

        // 创建一条新的状态记录，添加到状态表中去
        auth_status item(user_name, connect_key);
        _register_status_map.insert(std::pair<std::string, auth_status>(connect_key, item));

        // 将cb注册回调记录到本地
        auth_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.user_name = user_name;
        cb_item.cb = cb;
        cb_item.arg = arg;

        find->second.insert(std::pair<std::string, auth_cb_item>(connect_key, cb_item));

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性
        // 即：查询到的结果已经包含当前记录，因此避免查询-插入后还需要再进行一步检测的步骤
        // 问题：如果两个节点同时插入了记录，同时查询到记录，按照规则，可能都会被踢掉？
        //       但是踢掉也只是通过广播的形式来踢掉，没啥影响，大不了都登不上，都下线一次
        // 反正记录只能由注册者自己删除（或者说注册该连接的Caster维护）

        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX ACT:UND:%s EX %s FIELDS 1 %s %s",
                          user_name,
                          std::to_string(_key_expire_time).c_str(),
                          connect_key,
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理

        auto ctx = new auth_ctx();
        ctx->type = type;
        ctx->user_name = user_name;
        ctx->connect_key = connect_key;
        ctx->arg = arg;
        ctx->cb = cb;

        redisAsyncCommand(_pub_context, Redis_Add_Unname_Callback, ctx, "HGETALL ACT:UND:%s", user_name); // 查询当前频道的所有记录
    }
    else
    {
        // 先在本地的注册MAP中添加该记录
        auto find = _register_map.find(user_name);
        if (find == _register_map.end())
        {
            // 没有该用户的登录记录，创建一个map
            std::unordered_map<std::string, auth_cb_item> channel_cbs;
            _register_map.insert(std::pair<std::string, std::unordered_map<std::string, auth_cb_item>>(user_name, channel_cbs));
        }
        find = _register_map.find(user_name);

        // 创建一条新的状态记录，添加到状态表中去
        auth_status item(user_name, connect_key);
        _register_status_map.insert(std::pair<std::string, auth_status>(connect_key, item));

        // 将cb注册回调记录到本地
        auth_cb_item cb_item;
        cb_item.connect_key = connect_key;
        cb_item.user_name = user_name;
        cb_item.cb = cb;
        cb_item.arg = arg;

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
        ctx->connect_key = connect_key;
        ctx->arg = arg;
        ctx->cb = cb;

        redisAsyncCommand(_pub_context, Redis_Add_Login_Callback, ctx, "HGETALL ACT:REC:%s", user_name); // 查询当前频道的所有记录
    }
    return 0;
}

int auth_internal::add_logout_record(const char *user_name, const char *connect_key, AuthType type)
{

    if ((type == AuthType::SERVER && _anonymous_server_login) || (type == AuthType::CLIENT && _anonymous_client_login))
    {
        // 基站匿名登录 || 用户匿名登录

        // 删除成功
        // 从本地注册回调Map中删除指定的记录

        // 查询要注册的频道
        auto user_registers = _unnamed_map.find(user_name);
        if (user_registers == _unnamed_map.end())
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
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL ACT:UND:%s %s", user_name, connect_key);
    }
    else
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
    }

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
            // 更新匿名用户
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE ACT:UND:%s %s FIELDS 1 %s", items.second.user_name.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
        }
    }

    return 0;
}

int auth_internal::send_change_auth_status(const char *user_name, const char *connect_key, AuthReply status, const char *reason)
{
    // 向redis发布广播
    auth_broadcast_item item;

    item.type = AuthBroadcastType::ACCOUNT_STATUS_UPDATE;
    item.channel = user_name;
    item.connect_key = connect_key;
    item.Para = "";
    item.status = status;
    item.reason = reason;

    return redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH AUTH:BROADCAST %s", item.toString().c_str());

    return 0;
}

int auth_internal::broadcast_response(std::string req_str)
{
    // 根据接收到的广播，触发对应的回调函数，通知Catster外围创建和删除任务
    auth_broadcast_item req;
    if (req.fromString(req_str))
    {
        return 1; // 解析失败
    }

    std::unordered_map<std::string, std::unordered_map<std::string, auth_cb_item>> *item_map = nullptr;

    if (req.type == AuthBroadcastType::ACCOUNT_STATUS_UPDATE)
    {
        item_map = &_register_map;
    }
    else if (req.type == AuthBroadcastType::UNNAMED_STATUS_UPDATE)
    {
        item_map = &_unnamed_map;
    }
    else
    {
        return 1; // 不支持的广播类型
    }
    // 收到拉取激活源的请求
    auto item = item_map->find(req.channel);
    if (item == item_map->end())
    {
        return 2; // 本地没有该频道的注册记录
    }

    // 复制字符串
    auth_reply Reply;
    Reply.type = req.status;
    Reply.str = req.reason.c_str();

    if (req.connect_key.size() == 0) // 没有指定特定的连接，则对所有的连接都发送一次回复（针对允许同名频道都在线的情况）
    {
        for (auto iter : item->second)
        {
            auto cb_item = iter.second;
            auto Func = cb_item.cb;
            auto arg = cb_item.arg;
            Func(NULL, arg, &Reply);
        }
    }
    else
    {
        auto target = item->second.find(req.connect_key);
        if (target == item->second.end())
        {
            return 3; // 本地没有该连接的注册记录
        }

        auto cb_item = target->second;
        auto Func = cb_item.cb;
        auto arg = cb_item.arg;
        Func(NULL, arg, &Reply);
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
    // 接收广播信息，触发回调执行任务
    // 订阅到的是一个Json字符串

    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<auth_internal *>(privdata);

    if (!reply)
    {
        return;
    }
    if (reply->elements != 3)
    {
        return; // 异常的回调参数
    }

    auto re1 = reply->element[0];
    auto re2 = reply->element[1];
    auto re3 = reply->element[2];

    if (re3->type != REDIS_REPLY_STRING)
    {
        return; // 回复不是字符串，第一次订阅这个频道的时候回应为 REDIS_REPLY_INTEGER
    }

    svr->broadcast_response(re3->str);
}

void auth_internal::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<auth_internal *>(arg);

    // 判断过期用户

    // 本地维护的在线用户续期
    svr->upload_record_item();
}

void auth_internal::Redis_Add_Temp_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    // 判断是否添加成功

    // 本地添加登录限制记录（用户数量）

    auth_limit active_info;
    active_info._connect_limit = 9999; // 匿名用户默认允许非常多的连接

    auth_internal::getInstance()->_unnamed_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, active_info));

    auth_reply Reply;
    Reply.type = AuthReply::OK;
    ctx->cb(nullptr, ctx->arg, &Reply);

    delete ctx;
}

void auth_internal::Redis_Verify_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    if (!reply)
    {
        return;
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        auth_reply Reply;
        Reply.type = AuthReply::ERR; // AUTH_REPLY_ERR;
        Reply.str = "User Not active or existed!";
        ctx->cb(nullptr, ctx->arg, &Reply);
        return;
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        return;
    }

    // 解析查询到的信息

    // 判断密码是否一致

    // 将有效信息写入到本地记录中

    // 本地添加登录限制记录（用户数量）

    auth_limit active_info;
    active_info.fromString(reply->str);

    if (active_info._password != ctx->user_pwd)
    {
        auth_reply Reply;
        Reply.type = AuthReply::ERR; // AUTH_REPLY_ERR;
        Reply.str = "User Password Error!";
        ctx->cb(nullptr, ctx->arg, &Reply);
        return;
    }

    // 为了避免已经写入记录，这里需要先删除原有的记录
    auto find = auth_internal::getInstance()->_register_limit_map.find(ctx->user_name);
    if (find != auth_internal::getInstance()->_register_limit_map.end())
    {
        auth_internal::getInstance()->_register_limit_map.erase(find);
    }
    auth_internal::getInstance()->_register_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, active_info));
    // 解析查询到的信息,存储到本地

    // 如果不存在，那么就不允许登录
    auth_reply Reply;
    Reply.type = AuthReply::OK; // AUTH_REPLY_ERR;
    ctx->cb(nullptr, ctx->arg, &Reply);

    delete ctx;
}

void auth_internal::Redis_Add_Login_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    bool _check = false; // 检验记录中是否包含本条记录

    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::multimap<time_t, std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        if (strcmp(field, ctx->connect_key.c_str()) == 0)
        {
            _check = true;
        }
        records.insert(std::pair<std::time_t, std::string>(std::stol(value), field));
    }

    try
    {
        if (!_check)
        {
            throw std::logic_error("Can't Find Recored"); // 在返回的记录中不包含本条记录
        }

        //_check通过的话，records.size()必定大于等于1         // 查询当前账户允许在线的数量

        auto limit_item = auth_internal::getInstance()->_register_limit_map.find(ctx->user_name);
        if (limit_item == auth_internal::getInstance()->_register_limit_map.end())
        {
            throw std::logic_error("Can't Find User Active Info"); // 找不到用户的登录限制信息
        }

        while (records.size() > limit_item->second._connect_limit) // 有多个连接记录且设置不允许多个记录
        {
            if (auth_internal::getInstance()->_keep_early) // 已在线的优先级高，踢出当前
            {
                // 发送广播切换这个连接被踢出的连接的状态
                if (records.rbegin()->second == ctx->connect_key)
                {
                    _check = false;
                }
                else
                {
                    auth_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), records.rbegin()->second.c_str(), AuthReply::ERR, "User Connects Upper Limit , kick out this Connect!"); // 用户连接数已经到达上线，此连接被踢出
                }
                // 从records中删除最后一条记录(使用rbegin是为了保证切换状态和删除的是同一个连接)
                records.erase(records.rbegin()->first);
            }
            else
            {
                // 发送广播切换这个连接被踢出的连接的状态
                // 从records中删除这个连接
                records.erase(records.begin()->first);
                if (records.begin()->second == ctx->connect_key)
                {
                    _check = false;
                }
                else
                {
                    auth_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), records.begin()->second.c_str(), AuthReply::ERR, "User Connects Upper Limit , kick out this Connect!"); // 用户连接数已经到达上线，此连接被踢出
                }
            }
        }

        if (!_check) // 本条记录被踢出 不需要再发送OK的回调，  通过send_change_auth_status这个链路会使得这个连接接收到一个ERR的回调
        {
            throw std::logic_error("User Connects Upper Limit , kick out this Connect!"); // 在返回的记录中不包含本条记录
        }

        // else 有1个或者多个连接，但是允许多个记录
        // 正常，返回一个成功回调
        auth_reply Reply;
        Reply.type = AuthReply::OK;
        Reply.str = "";
        ctx->cb(NULL, ctx->arg, &Reply);
    }
    catch (const std::exception &e)
    {
        auth_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), ctx->connect_key.c_str(), AuthReply::ERR, e.what()); //
    }

    delete ctx;
}

void auth_internal::Redis_Add_Unname_Callback(redisAsyncContext *c, void *r, void *privdata)
{

    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    bool _check = false; // 检验记录中是否包含本条记录

    // 从回复中读取所有的有效记录（有效记录，更新时间没有差异过大，差异过大则认为是已经挂掉的连接）
    std::multimap<time_t, std::string> records; // 有效记录
    for (int i = 0; i < reply->elements; i += 2)
    {
        auto field = reply->element[i]->str;
        std::string value = reply->element[i + 1]->str;

        if (strcmp(field, ctx->connect_key.c_str()) == 0)
        {
            _check = true;
        }
        records.insert(std::pair<std::time_t, std::string>(std::stol(value), field));
    }
    try
    {
        if (!_check)
        {
            throw std::logic_error("Can't Find Recored"); // 在返回的记录中不包含本条记录
        }

        // else 有1个或者多个连接，但是允许多个记录
        // 正常，返回一个成功回调
        auth_reply Reply;
        Reply.type = AuthReply::OK;
        Reply.str = "";
        ctx->cb(NULL, ctx->arg, &Reply);
    }
    catch (const std::exception &e)
    {
        auth_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), records.begin()->second.c_str(), AuthReply::ERR, e.what()); //
    }

    delete ctx;
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

int auth_broadcast_item::fromString(const std::string &str)
{
    json info = json::parse(str);
    try
    {
        type = static_cast<AuthBroadcastType>(info["type"].get<int>());
        connect_key = info["connect_key"].get<std::string>();
        channel = info["channel"].get<std::string>();
        Para = info["Para"].get<std::string>();
        status = static_cast<AuthReply>(info["status"].get<int>());
        reason = info["reason"].get<std::string>();
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[auth_broadcast_item:{}]: decode field: {} ,what: {}", __func__, str, e.what());
        return 1;
    }

    return 0;
}

std::string auth_broadcast_item::toString()
{
    json info;

    info["type"] = static_cast<int>(type);
    info["connect_key"] = connect_key;
    info["channel"] = channel;
    info["Para"] = Para;
    info["status"] = static_cast<int>(status);
    info["reason"] = reason;

    return info.dump();
}

int auth_limit::fromString(const std::string &str)
{
    json info = json::parse(str);
    try
    {
        _account = info["account"].get<std::string>();
        _password = info["password"].get<std::string>();
        _active = info["active"].get<bool>();
        _type = info["type"].get<int>();
        _date_limit = info["date_limit"].get<int>();
        _time_limit = info["time_limit"].get<int>();
        _access = info["access"].get<int>();
        _connect_limit = info["connect_limit"].get<int>();
        _group = info["group"].get<std::string>();
        _expire = info["expire"].get<int>();
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[auth_limit:{}]: decode field: {} ,what: {}", __func__, str, e.what());
        return 1;
    }
    return 0;
}
