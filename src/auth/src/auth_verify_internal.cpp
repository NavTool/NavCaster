#include "auth_verify_internal.h"
#include "auth_login_service.h"
#include "auth_record_limit.h"
#include "auth_session_record.h"
#include "access_runtime_service.h"
#include "account_schema.h"
#include "json_record.h"
#include "log_observability.h"
#include "redis_keys.h"
#include <algorithm>
#include <list>
#include <spdlog/spdlog.h>
#include "knt.h"

namespace
{
std::string normalize_group_uid(const std::string &group_uid)
{
    return group_uid.empty() ? "default" : group_uid;
}

navcaster::auth::AuthLoginOptions current_login_options(const verify_internal &svr)
{
    navcaster::auth::AuthLoginOptions options;
    options.server_anonymous_login = svr.server_anonymous_login();
    options.client_anonymous_login = svr.client_anonymous_login();
    options.source_anonymous_login = svr.source_anonymous_login();
    options.server_online_protection = svr.server_online_protection();
    options.client_online_protection = svr.client_online_protection();
    return options;
}

const char *auth_reply_name(AuthReply reply)
{
    switch (reply)
    {
    case AuthReply::ERR:
        return "err";
    case AuthReply::OK:
        return "ok";
    case AuthReply::ACTIVE:
        return "active";
    case AuthReply::INACTIVE:
        return "inactive";
    case AuthReply::STRING:
        return "string";
    case AuthReply::INTEGER:
        return "integer";
    case AuthReply::DOUBLE:
        return "double";
    case AuthReply::NIL:
        return "nil";
    }
    return "unknown";
}

std::string append_key(const char *prefix, const std::string &id)
{
    return std::string(prefix) + id;
}

void delete_auth_ctx(auth_ctx *ctx)
{
    if (!ctx)
    {
        return;
    }
    delete ctx->active_info;
    ctx->active_info = nullptr;
    delete ctx;
}

void reject_auth_ctx(auth_ctx *ctx, const std::string &reason, const std::string &legacy_reply = "User Not active or existed!")
{
    if (!ctx)
    {
        return;
    }
    spdlog::warn("[auth]: event=login_rejected operation=verify_account auth_type={} account={} reason={}",
                 navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                 ctx->user_name,
                 reason);
    auth_reply Reply;
    Reply.type = AuthReply::ERR;
    Reply.str = legacy_reply.c_str();
    if (ctx->cb)
    {
        ctx->cb(nullptr, ctx->arg, &Reply);
    }
    delete_auth_ctx(ctx);
}

bool redis_reply_to_json_object(redisReply *reply, json &out, std::string &reason)
{
    if (!reply)
    {
        reason = "redis_reply_missing";
        return false;
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        reason = "not_found";
        return false;
    }
    if (reply->type != REDIS_REPLY_STRING || !reply->str)
    {
        reason = "unexpected_reply_type";
        return false;
    }
    try
    {
        out = json::parse(reply->str);
    }
    catch (const std::exception &e)
    {
        reason = e.what();
        return false;
    }
    if (!out.is_object())
    {
        reason = "record_not_object";
        return false;
    }
    return true;
}

bool json_status_active(const json &record)
{
    return record.is_object() && record.value("status", std::string{}) == navcaster::core::ACCESS_RUNTIME_STATUS_ACTIVE;
}

void continue_legacy_verify(auth_ctx *ctx)
{
    redisAsyncCommand(verify_internal::getInstance()->_pub_context,
                      verify_internal::Redis_Verify_Callback,
                      ctx,
                      "HGET ACT:ACTIVE %s",
                      ctx->user_name.c_str());
}

navcaster::core::AccessRuntimeRecordInput runtime_input_from_item(const auth_cb_item &item,
                                                                  std::time_t update_time,
                                                                  const char *disconnect_reason)
{
    navcaster::core::AccessRuntimeRecordInput input;
    input.owner_account_id = item.owner_account_id;
    input.access_account_id = item.access_account_id;
    input.access_username = item.access_username.empty() ? item.user_name : item.access_username;
    input.access_kind = item.access_kind;
    input.mountpoint = item.runtime.mountpoint;
    input.group_id = item.mount_point_group_id.empty() ? item.group_uid : item.mount_point_group_id;
    input.connect_key = item.connect_key;
    input.auth_type = navcaster::auth::AuthLoginService::auth_type_name(item.type);
    input.addr = item.runtime.addr;
    input.port = item.runtime.port;
    input.user_agent = item.runtime.user_agent;
    input.ntrip_version = item.runtime.ntrip_version;
    input.billing_mode = item.billing_mode.empty() ? navcaster::core::ACCESS_RUNTIME_BILLING_MODE_PAYG : item.billing_mode;
    input.start_time = item.online_time;
    input.update_time = update_time;
    input.end_time = update_time;
    input.used_seconds = std::max<std::int64_t>(0, static_cast<std::int64_t>(update_time - item.online_time));
    input.stat_cost_cents = navcaster::core::calculate_runtime_cost_cents(input.used_seconds, item.hourly_price_cents, item.billing_multiplier);
    input.actual_debit_cents =
        item.access_kind == navcaster::core::ACCESS_RUNTIME_KIND_USER_CLIENT &&
                input.billing_mode == navcaster::core::ACCESS_RUNTIME_BILLING_MODE_PAYG
            ? input.stat_cost_cents
            : 0;
    input.balance_after_cents = item.balance_cents - input.actual_debit_cents;
    input.disconnect_reason = disconnect_reason && *disconnect_reason ? disconnect_reason : item.runtime.disconnect_reason;
    if (input.disconnect_reason.empty())
    {
        input.disconnect_reason = "client_closed";
    }
    return input;
}

std::int64_t json_i64_value(const json &record, const char *field, std::int64_t fallback = 0)
{
    auto it = record.find(field);
    return it == record.end() ? fallback : navcaster::json_record::as_i64(*it, fallback);
}

struct access_runtime_revalidation_ctx
{
    std::string user_name;
    std::string connect_key;
    std::string access_username;
    std::string auth_type;
    std::string mountpoint;
    std::int64_t now = 0;
    std::int64_t hourly_price_cents = 0;
    double billing_multiplier = 1.0;
};
}

verify_internal::verify_internal(/* args */)
{
}

verify_internal::~verify_internal()
{
}

verify_internal *verify_internal::getInstance()
{
    static verify_internal instance;
    return &instance;
}

int verify_internal::init(AuthVerifyOpt opt, event_base *base)
{

    _server_anonymous_login = opt.base_anonymous_login();
    _server_online_protection = opt.base_online_protection();
    _client_anonymous_login = opt.rover_anonymous_login();
    _client_online_protection = opt.rover_online_protection();
    _source_anonymous_login = opt.source_anonymous_login();

    _redis_IP = opt.redis_host();
    _redis_port = opt.redis_port();
    _redis_Requirepass = opt.redis_password();

    _base = base;
    return 0;
}
int verify_internal::start()
{
    pubAttemptReconnect();
    subAttemptReconnect();

    _timeout_tv.tv_sec = _update_intv;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);
    event_add(_timeout_ev, &_timeout_tv);

    return 0;
}

int verify_internal::stop()
{
    if (_sub_context)
    {
        redisAsyncDisconnect(_sub_context);
        redisAsyncFree(_sub_context);
    }
    if (_pub_context)
    {
        redisAsyncDisconnect(_pub_context);
        redisAsyncFree(_pub_context);
    }
    return 0;
}

int verify_internal::verify(const char *user_name, const char *user_pwd, VerifyCallback cb, void *arg, AuthType type, const AuthRuntimeContext *runtime)
{
    auto ctx = new auth_ctx;
    ctx->type = type;
    ctx->user_name = user_name ? user_name : "";
    ctx->user_pwd = user_pwd ? user_pwd : "";
    ctx->cb = cb;
    ctx->arg = arg;
    if (runtime)
    {
        ctx->runtime = *runtime;
        ctx->has_runtime = true;
    }

    const bool anonymous_login = navcaster::auth::AuthLoginService::anonymous_enabled(type, current_login_options(*this));
    spdlog::info("[auth]: event=verify_request auth_type={} account={} anonymous={}",
                 navcaster::auth::AuthLoginService::auth_type_name(type),
                 user_name,
                 anonymous_login ? "true" : "false");
    if (anonymous_login)
    {
        //     如果是匿名模式，那么账户系统就完全失效，只会生效ACT:UNNAMED
        //     基站匿名登录 || 用户匿名登录
        //     自动注册一个匿名账户
        redisAsyncCommand(_pub_context, Redis_Add_Temp_Callback, ctx, "HSET ACT:UNNAMED %s %s", ctx->user_name.c_str(), util_get_time_stamp_str().c_str());
    }
    else
    {
        redisAsyncCommand(_pub_context, Redis_Verify_Access_Callback, ctx, "HGET AACC:ACTIVE %s", ctx->user_name.c_str());
    }

    // //
    // if ((type == AuthType::SERVER && _anonymous_server_login) || (type == AuthType::CLIENT && _anonymous_client_login))
    // {
    //     // 基站匿名登录 || 用户匿名登录
    //     // 自动注册一个匿名账户
    //
    // }
    // else if (type == AuthType::SOURCE)
    // {
    // }
    // else
    // {
    // 查询这个账户的相关信息，等待回调

    // }
    return 0;
}

int verify_internal::add_login_record(const char *user_name, const char *connect_key, VerifyCallback cb, void *arg, AuthType type, const AuthRuntimeContext *runtime)
{

    if (navcaster::auth::AuthLoginService::anonymous_enabled(type, current_login_options(*this)) && type != AuthType::SOURCE)
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
        cb_item.connect_key = connect_key ? connect_key : "";
        cb_item.user_name = user_name ? user_name : "";
        cb_item.type = type;
        cb_item.online_time = util_get_time_stamp();
        cb_item.group_uid = "default";
        if (runtime)
        {
            cb_item.runtime = *runtime;
        }
        cb_item.cb = cb;
        cb_item.arg = arg;

        find->second.insert(std::pair<std::string, auth_cb_item>(cb_item.connect_key, cb_item));

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性
        // 即：查询到的结果已经包含当前记录，因此避免查询-插入后还需要再进行一步检测的步骤
        // 问题：如果两个节点同时插入了记录，同时查询到记录，按照规则，可能都会被踢掉？
        //       但是踢掉也只是通过广播的形式来踢掉，没啥影响，大不了都登不上，都下线一次
        // 反正记录只能由注册者自己删除（或者说注册该连接的Caster维护）

        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX ACT:UND:%s EX %s FIELDS 1 %s %s",
                          cb_item.user_name.c_str(),
                          std::to_string(_key_expire_time).c_str(),
                          cb_item.connect_key.c_str(),
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理

        auto ctx = new auth_ctx();
        ctx->type = type;
        ctx->user_name = cb_item.user_name;
        ctx->connect_key = cb_item.connect_key;
        ctx->arg = arg;
        ctx->cb = cb;
        if (runtime)
        {
            ctx->runtime = *runtime;
            ctx->has_runtime = true;
        }

        redisAsyncCommand(_pub_context, Redis_Add_Unname_Callback, ctx, "HGETALL ACT:UND:%s", cb_item.user_name.c_str()); // 查询当前频道的所有记录
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
        cb_item.connect_key = connect_key ? connect_key : "";
        cb_item.user_name = user_name ? user_name : "";
        cb_item.type = type;
        cb_item.online_time = util_get_time_stamp();
        if (runtime)
        {
            cb_item.runtime = *runtime;
        }
        cb_item.cb = cb;
        cb_item.arg = arg;

        find->second.insert(std::pair<std::string, auth_cb_item>(cb_item.connect_key, cb_item));

        // 先向云端插入该条记录，再查询记录，这样能够保证原子性
        // 即：查询到的结果已经包含当前记录，因此避免查询-插入后还需要再进行一步检测的步骤
        // 问题：如果两个节点同时插入了记录，同时查询到记录，按照规则，可能都会被踢掉？
        //       但是踢掉也只是通过广播的形式来踢掉，没啥影响，大不了都登不上，都下线一次
        // 反正记录只能由注册者自己删除（或者说注册该连接的Caster维护）

        // 向云端插入记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX ACT:REC:%s EX %s FIELDS 1 %s %s",
                          cb_item.user_name.c_str(),
                          std::to_string(_key_expire_time).c_str(),
                          cb_item.connect_key.c_str(),
                          util_get_time_stamp_str().c_str()); // 更新挂载点数据生产者的更新时间

        // 向云端查询记录，等待下一步处理

        auto ctx = new auth_ctx();
        ctx->type = type;
        ctx->user_name = cb_item.user_name;
        ctx->connect_key = cb_item.connect_key;
        ctx->arg = arg;
        ctx->cb = cb;
        if (runtime)
        {
            ctx->runtime = *runtime;
            ctx->has_runtime = true;
        }

        redisAsyncCommand(_pub_context, Redis_Add_Login_Callback, ctx, "HGETALL ACT:REC:%s", cb_item.user_name.c_str()); // 查询当前频道的所有记录
    }
    return 0;
}

int verify_internal::add_logout_record(const char *user_name, const char *connect_key, AuthType type, const AuthRuntimeContext *runtime)
{

    if (navcaster::auth::AuthLoginService::anonymous_enabled(type, current_login_options(*this)) && type != AuthType::SOURCE)
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
        auto cb_item = item->second;
        if (runtime)
        {
            cb_item.runtime = *runtime;
        }
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
        auto cb_item = item->second;
        if (runtime)
        {
            cb_item.runtime = *runtime;
        }
        finalize_access_runtime_session(cb_item, cb_item.runtime.disconnect_reason.c_str());
        user_registers->second.erase(item);
        // 删除Redis记录
        redisAsyncCommand(_pub_context, NULL, NULL, "HDEL ACT:REC:%s %s", user_name, connect_key);
        remove_active_session(user_name, connect_key);
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

int verify_internal::init_sub_context()
{
    redisAsyncCommand(_sub_context, Redis_Broadcast_Callback, this, "SUBSCRIBE AUTH:BROADCAST");
    return 0;
}

int verify_internal::init_pub_context()
{
    redisAsyncCommand(_pub_context, NULL, NULL, "DEL MPT:STAT");
    redisAsyncCommand(_pub_context, NULL, NULL, "DEL USR:STAT");
    return 0;
}

int verify_internal::subAttemptReconnect()
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

int verify_internal::pubAttemptReconnect()
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

int verify_internal::upload_record_item()
{
    for (auto iter : _register_map)
    {
        for (auto items : iter.second)
        {
            // 更新注册用户
            redisAsyncCommand(_pub_context, NULL, NULL, "HEXPIRE ACT:REC:%s %s FIELDS 1 %s", items.second.user_name.c_str(), std::to_string(_key_expire_time).c_str(), items.second.connect_key.c_str()); // 给这个挂载点连接续期
            if (items.second.active_session_enabled)
            {
                const auto update_time = util_get_time_stamp();
                update_active_session(items.second, update_time);
                update_access_online_session(items.second, update_time);
                revalidate_access_runtime_session(items.second, update_time);
            }
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

int verify_internal::send_change_auth_status(const char *user_name, const char *connect_key, AuthReply status, const char *reason)
{
    // 向redis发布广播
    auth_broadcast_item item;

    item.type = AuthBroadcastType::ACCOUNT_STATUS_UPDATE;
    item.channel = user_name;
    item.connect_key = connect_key;
    item.Para = "";
    item.status = status;
    item.reason = reason;

    spdlog::warn("[auth]: event=auth_broadcast_publish operation=send_change_auth_status account={} connect_key={} status={} reason={}",
                 user_name ? user_name : "",
                 connect_key ? connect_key : "",
                 auth_reply_name(status),
                 reason ? reason : "");

    const int ret = redisAsyncCommand(_pub_context, NULL, NULL, "PUBLISH AUTH:BROADCAST %s", item.toString().c_str());
    if (ret != REDIS_OK)
    {
        spdlog::error("[auth]: event=redis_command_failed operation=send_change_auth_status redis_key=AUTH:BROADCAST account={} connect_key={} reason=publish_failed",
                      user_name ? user_name : "",
                      connect_key ? connect_key : "");
    }
    return ret;

    return 0;
}

int verify_internal::remove_active_session(const char *user_name, const char *connect_key)
{
    if (!_pub_context || !_is_pub_connected)
    {
        spdlog::warn("[auth]: event=active_session_remove_skipped operation=remove_active_session redis_key={} account={} connect_key={} reason=redis_not_connected",
                     navcaster::auth::active_session_key(user_name ? user_name : ""),
                     user_name ? user_name : "",
                     connect_key ? connect_key : "");
        return REDIS_ERR;
    }
    const auto redis_key = navcaster::auth::active_session_key(user_name ? user_name : "");
    spdlog::info("[auth]: event=active_session_remove operation=remove_active_session redis_key={} account={} connect_key={}",
                 redis_key,
                 user_name ? user_name : "",
                 connect_key ? connect_key : "");
    const int ret = redisAsyncCommand(_pub_context, NULL, NULL, "HDEL %s %s", redis_key.c_str(), connect_key);
    if (ret != REDIS_OK)
    {
        spdlog::error("[auth]: event=redis_command_failed operation=remove_active_session redis_key={} account={} connect_key={} reason=hdel_failed",
                      redis_key,
                      user_name ? user_name : "",
                      connect_key ? connect_key : "");
    }
    return ret;
}

int verify_internal::update_active_session(const auth_cb_item &item, std::time_t update_time)
{
    if (!_pub_context || !_is_pub_connected)
    {
        spdlog::warn("[auth]: event=active_session_update_skipped operation=update_active_session redis_key={} account={} connect_key={} reason=redis_not_connected",
                     navcaster::auth::active_session_key(item.user_name),
                     item.user_name,
                     item.connect_key);
        return REDIS_ERR;
    }
    const auto online_time = item.online_time > 0 ? item.online_time : update_time;
    const auto redis_key = navcaster::auth::active_session_key(item.user_name);
    spdlog::debug("[auth]: event=active_session_update operation=update_active_session redis_key={} account={} connect_key={} auth_type={} group_uid={}",
                  redis_key,
                  item.user_name,
                  item.connect_key,
                  navcaster::auth::AuthLoginService::auth_type_name(item.type),
                  item.group_uid);
    const int ret = redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX %s EX %s FIELDS 1 %s %s",
                             redis_key.c_str(),
                             std::to_string(_key_expire_time).c_str(),
                             item.connect_key.c_str(),
                             navcaster::auth::build_active_session_record_json(item.user_name, item.connect_key, item.type, online_time, update_time, item.group_uid).c_str());
    if (ret != REDIS_OK)
    {
        spdlog::error("[auth]: event=redis_command_failed operation=update_active_session redis_key={} account={} connect_key={} reason=hsetex_failed",
                      redis_key,
                      item.user_name,
                      item.connect_key);
    }
    return ret;
}

int verify_internal::remove_access_online_session(const auth_cb_item &item)
{
    if (!item.access_runtime_enabled || item.owner_account_id.empty())
    {
        return REDIS_OK;
    }
    if (!_pub_context || !_is_pub_connected)
    {
        spdlog::warn("[auth]: event=access_online_session_remove_skipped operation=remove_access_online_session owner_account_id={} access_account_id={} connect_key={} reason=redis_not_connected",
                     item.owner_account_id,
                     item.access_account_id,
                     item.connect_key);
        return REDIS_ERR;
    }
    const std::string redis_key = append_key(navcaster::redis_keys::ONLINE_SESSION_PREFIX, item.owner_account_id);
    const int ret = redisAsyncCommand(_pub_context, NULL, NULL, "HDEL %s %s", redis_key.c_str(), item.connect_key.c_str());
    if (ret != REDIS_OK)
    {
        spdlog::error("[auth]: event=redis_command_failed operation=remove_access_online_session redis_key={} owner_account_id={} access_account_id={} connect_key={} reason=hdel_failed",
                      redis_key,
                      item.owner_account_id,
                      item.access_account_id,
                      item.connect_key);
    }
    return ret;
}

int verify_internal::update_access_online_session(const auth_cb_item &item, std::time_t update_time)
{
    if (!item.access_runtime_enabled || item.owner_account_id.empty())
    {
        return REDIS_OK;
    }
    if (!_pub_context || !_is_pub_connected)
    {
        spdlog::warn("[auth]: event=access_online_session_update_skipped operation=update_access_online_session owner_account_id={} access_account_id={} connect_key={} reason=redis_not_connected",
                     item.owner_account_id,
                     item.access_account_id,
                     item.connect_key);
        return REDIS_ERR;
    }
    auto input = runtime_input_from_item(item, update_time, nullptr);
    input.update_time = update_time;
    const auto record = navcaster::core::build_online_session_record(input).dump();
    const std::string redis_key = append_key(navcaster::redis_keys::ONLINE_SESSION_PREFIX, item.owner_account_id);
    const int ret = redisAsyncCommand(_pub_context, NULL, NULL, "HSETEX %s EX %s FIELDS 1 %s %s",
                                      redis_key.c_str(),
                                      std::to_string(_key_expire_time).c_str(),
                                      item.connect_key.c_str(),
                                      record.c_str());
    if (ret != REDIS_OK)
    {
        spdlog::error("[auth]: event=redis_command_failed operation=update_access_online_session redis_key={} owner_account_id={} access_account_id={} connect_key={} reason=hsetex_failed",
                      redis_key,
                      item.owner_account_id,
                      item.access_account_id,
                      item.connect_key);
    }
    return ret;
}

int verify_internal::write_access_runtime_login(const auth_cb_item &item, std::time_t update_time)
{
    if (!item.access_runtime_enabled)
    {
        return REDIS_OK;
    }
    int ret = update_access_online_session(item, update_time);
    if (ret != REDIS_OK)
    {
        return ret;
    }
    return REDIS_OK;
}

int verify_internal::revalidate_access_runtime_session(const auth_cb_item &item, std::time_t update_time)
{
    if (!item.access_runtime_enabled || item.access_username.empty())
    {
        return REDIS_OK;
    }
    if (!_pub_context || !_is_pub_connected)
    {
        spdlog::warn("[auth]: event=access_runtime_revalidation_skipped operation=revalidate_access_runtime_session owner_account_id={} access_account_id={} connect_key={} reason=redis_not_connected",
                     item.owner_account_id,
                     item.access_account_id,
                     item.connect_key);
        return REDIS_ERR;
    }

    auto *ctx = new access_runtime_revalidation_ctx;
    ctx->user_name = item.user_name;
    ctx->connect_key = item.connect_key;
    ctx->access_username = item.access_username;
    ctx->auth_type = navcaster::auth::AuthLoginService::auth_type_name(item.type);
    ctx->mountpoint = item.runtime.mountpoint;
    ctx->now = update_time;
    ctx->hourly_price_cents = item.hourly_price_cents;
    ctx->billing_multiplier = item.billing_multiplier;
    const int ret = redisAsyncCommand(_pub_context,
                                      Redis_Revalidate_Access_Runtime_Callback,
                                      ctx,
                                      "HGET %s %s",
                                      navcaster::redis_keys::AACC_ACTIVE,
                                      item.access_username.c_str());
    if (ret != REDIS_OK)
    {
        delete ctx;
        spdlog::error("[auth]: event=redis_command_failed operation=revalidate_access_runtime_session redis_key={} access_username={} connect_key={} reason=hget_failed",
                      navcaster::redis_keys::AACC_ACTIVE,
                      item.access_username,
                      item.connect_key);
    }
    return ret;
}

int verify_internal::finalize_access_runtime_session(const auth_cb_item &item, const char *disconnect_reason)
{
    if (!item.access_runtime_enabled)
    {
        return REDIS_OK;
    }
    if (!_pub_context || !_is_pub_connected)
    {
        spdlog::warn("[auth]: event=access_runtime_finalize_skipped operation=finalize_access_runtime_session owner_account_id={} access_account_id={} connect_key={} reason=redis_not_connected",
                     item.owner_account_id,
                     item.access_account_id,
                     item.connect_key);
        return REDIS_ERR;
    }
    const auto now = util_get_time_stamp();
    auto input = runtime_input_from_item(item, now, disconnect_reason);
    const std::string period = navcaster::core::runtime_period_from_unix(now);
    const auto billing = navcaster::core::build_billing_usage_entry(input);
    const std::string billing_id = billing.value("billing_id", std::string{});
    const std::string fingerprint = billing.value("fingerprint", std::string{});
    const std::string idempotent_payload = json{{"fingerprint", fingerprint}, {"period", period}, {"create_time", now}}.dump();
    const std::string bill_entry_key = append_key(navcaster::redis_keys::BILL_ENTRY_PREFIX, period);
    const std::string bill_account_key = append_key(navcaster::redis_keys::BILL_ACCOUNT_PREFIX, item.owner_account_id) + ":" + period;
    const std::string ledger_key = append_key(navcaster::redis_keys::ACC_BALANCE_LEDGER_PREFIX, period);
    const auto ledger = input.actual_debit_cents != 0
                            ? navcaster::core::build_balance_ledger_entry(input)
                            : json::object();
    const std::string ledger_id = ledger.value("ledger_id", std::string{});
    const std::string ledger_payload = ledger.empty() ? std::string{} : ledger.dump();
    static constexpr const char *billing_script =
        "if redis.call('HSETNX',KEYS[1],ARGV[1],ARGV[2])==0 then return 0 end "
        "redis.call('HSETNX',KEYS[2],ARGV[1],ARGV[3]) "
        "redis.call('LPUSH',KEYS[3],ARGV[1]) "
        "local debit=tonumber(ARGV[7]) or 0 "
        "if debit~=0 then "
        "local raw=redis.call('HGET',KEYS[4],ARGV[4]) "
        "if raw then "
        "local ok,rec=pcall(cjson.decode,raw) "
        "if ok and type(rec)=='table' then "
        "local balance=tonumber(rec['balance_cents'] or 0) or 0 "
        "rec['balance_cents']=balance-debit "
        "rec['update_time']=tonumber(ARGV[8]) or rec['update_time'] "
        "redis.call('HSET',KEYS[4],ARGV[4],cjson.encode(rec)) "
        "end end "
        "local araw=redis.call('HGET',KEYS[6],ARGV[9]) "
        "if araw then "
        "local aok,arec=pcall(cjson.decode,araw) "
        "if aok and type(arec)=='table' then "
        "local abalance=tonumber(arec['balance_cents'] or 0) or 0 "
        "arec['balance_cents']=abalance-debit "
        "arec['update_time']=tonumber(ARGV[8]) or arec['update_time'] "
        "redis.call('HSET',KEYS[6],ARGV[9],cjson.encode(arec)) "
        "end end "
        "redis.call('HSETNX',KEYS[5],ARGV[5],ARGV[6]) "
        "end "
        "return 1";
    int ret = redisAsyncCommand(_pub_context, NULL, NULL, "EVAL %s 6 %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s",
                                billing_script,
                                navcaster::redis_keys::BILL_IDEMPOTENT,
                                bill_entry_key.c_str(),
                                bill_account_key.c_str(),
                                navcaster::redis_keys::ACC_RECORD,
                                ledger_key.c_str(),
                                navcaster::redis_keys::AACC_ACTIVE,
                                billing_id.c_str(),
                                idempotent_payload.c_str(),
                                billing.dump().c_str(),
                                item.owner_account_id.c_str(),
                                ledger_id.c_str(),
                                ledger_payload.c_str(),
                                std::to_string(input.actual_debit_cents).c_str(),
                                std::to_string(now).c_str(),
                                input.access_username.c_str());
    if (ret != REDIS_OK)
    {
        return ret;
    }

    if (item.access_kind == navcaster::core::ACCESS_RUNTIME_KIND_SUPPLIER_STATION)
    {
        const auto supply = navcaster::core::build_supplier_supply_usage(input);
        const std::string supply_key = append_key(navcaster::redis_keys::SUPPLY_USAGE_PREFIX, period);
        const std::string supply_account_key = append_key(navcaster::redis_keys::SUPPLY_ACCOUNT_PREFIX, item.owner_account_id) + ":" + period;
        ret = redisAsyncCommand(_pub_context, NULL, NULL, "HSETNX %s %s %s",
                                supply_key.c_str(),
                                supply.value("usage_id", std::string{}).c_str(),
                                supply.dump().c_str());
        if (ret != REDIS_OK)
        {
            return ret;
        }
        redisAsyncCommand(_pub_context, NULL, NULL, "LPUSH %s %s", supply_account_key.c_str(), supply.value("usage_id", std::string{}).c_str());

        const auto login_event = navcaster::core::build_station_event(input, "login");
        const auto station = navcaster::core::build_station_record(input, false);
        const auto disconnect_event = navcaster::core::build_station_event(input, "disconnect");
        const std::string event_key = append_key(navcaster::redis_keys::STATION_EVENT_PREFIX, input.mountpoint);
        ret = redisAsyncCommand(_pub_context, NULL, NULL, "HSET %s %s %s",
                                navcaster::redis_keys::STATION_RECORD,
                                input.mountpoint.c_str(),
                                station.dump().c_str());
        if (ret != REDIS_OK)
        {
            return ret;
        }
        ret = redisAsyncCommand(_pub_context, NULL, NULL, "RPUSH %s %s", event_key.c_str(), login_event.dump().c_str());
        if (ret != REDIS_OK)
        {
            return ret;
        }
        ret = redisAsyncCommand(_pub_context, NULL, NULL, "LPUSH %s %s", event_key.c_str(), disconnect_event.dump().c_str());
        if (ret != REDIS_OK)
        {
            return ret;
        }
    }

    remove_access_online_session(item);
    spdlog::info("[auth]: event=access_runtime_finalized owner_account_id={} access_account_id={} mountpoint={} connect_key={} used_seconds={} debit_cents={} reason={}",
                 item.owner_account_id,
                 item.access_account_id,
                 input.mountpoint,
                 item.connect_key,
                 input.used_seconds,
                 input.actual_debit_cents,
                 input.disconnect_reason);
    return REDIS_OK;
}

int verify_internal::broadcast_response(std::string req_str)
{
    // 根据接收到的广播，触发对应的回调函数，通知Catster外围创建和删除任务
    auth_broadcast_item req;
    if (req.fromString(req_str))
    {
        spdlog::warn("[auth]: event=auth_broadcast_decode_failed operation=broadcast_response reason=parse_error");
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
        spdlog::warn("[auth]: event=auth_broadcast_ignored operation=broadcast_response account={} connect_key={} reason=unsupported_type",
                     req.channel,
                     req.connect_key);
        return 1; // 不支持的广播类型
    }
    // 收到拉取激活源的请求
    auto item = item_map->find(req.channel);
    if (item == item_map->end())
    {
        spdlog::debug("[auth]: event=auth_broadcast_no_local_record operation=broadcast_response account={} connect_key={} status={} reason=no_channel",
                      req.channel,
                      req.connect_key,
                      auth_reply_name(req.status));
        return 2; // 本地没有该频道的注册记录
    }

    // 复制字符串
    auth_reply Reply;
    Reply.type = req.status;
    Reply.str = req.reason.c_str();

    const bool disable_active_session =
        req.type == AuthBroadcastType::ACCOUNT_STATUS_UPDATE &&
        (req.status == AuthReply::ERR || req.status == AuthReply::INACTIVE);

    if (req.connect_key.size() == 0) // 没有指定特定的连接，则对所有的连接都发送一次回复（针对允许同名频道都在线的情况）
    {
        spdlog::warn("[auth]: event=auth_broadcast_apply operation=broadcast_response account={} connect_key=* status={} reason={}",
                     req.channel,
                     auth_reply_name(req.status),
                     req.reason);
        for (auto &iter : item->second)
        {
            if (disable_active_session)
            {
                iter.second.active_session_enabled = false;
                finalize_access_runtime_session(iter.second, req.reason.c_str());
                remove_active_session(req.channel.c_str(), iter.second.connect_key.c_str());
            }
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
            spdlog::debug("[auth]: event=auth_broadcast_no_local_record operation=broadcast_response account={} connect_key={} status={} reason=no_connect_key",
                          req.channel,
                          req.connect_key,
                          auth_reply_name(req.status));
            return 3; // 本地没有该连接的注册记录
        }

        spdlog::warn("[auth]: event=auth_broadcast_apply operation=broadcast_response account={} connect_key={} status={} reason={}",
                     req.channel,
                     req.connect_key,
                     auth_reply_name(req.status),
                     req.reason);

        if (disable_active_session)
        {
            target->second.active_session_enabled = false;
            finalize_access_runtime_session(target->second, req.reason.c_str());
            remove_active_session(req.channel.c_str(), req.connect_key.c_str());
        }
        auto cb_item = target->second;
        auto Func = cb_item.cb;
        auto arg = cb_item.arg;
        Func(NULL, arg, &Reply);
    }

    return 0;
}

void verify_internal::Redis_Pub_Connect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<verify_internal *>(c->data);

    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
        svr->_is_pub_connected = true;
        svr->init_pub_context();
    }
    else
    {
        svr->_is_pub_connected = false;
        svr->_pub_context_errstr = c->errstr ? c->errstr : "unknown";
        spdlog::error("[{}:{}]: Redis pub connection failed: {}", __class__, __func__, svr->_pub_context_errstr);
        svr->_pub_context = nullptr; /* avoid stale pointer when callback returns */
        return;
    }
    svr->pubAttemptReconnect();
}

void verify_internal::Redis_Sub_Connect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<verify_internal *>(c->data);

    if (status == REDIS_OK)
    {
        spdlog::info("[{}:{}]: Connected to Redis Success", __class__, __func__);
        svr->_is_sub_connected = true;
        svr->init_sub_context();
    }
    else
    {
        svr->_is_sub_connected = false;
        svr->_sub_context_errstr = c->errstr ? c->errstr : "unknown";
        spdlog::error("[{}:{}]: Redis sub connection failed: {}", __class__, __func__, svr->_sub_context_errstr);
        svr->_sub_context = nullptr; /* avoid stale pointer when callback returns */
        return;
    }
    svr->subAttemptReconnect();
}

void verify_internal::Redis_Pub_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<verify_internal *>(c->data);

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

void verify_internal::Redis_Sub_Disconnect_Cb(const redisAsyncContext *c, int status)
{
    auto svr = static_cast<verify_internal *>(c->data);

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

void verify_internal::Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    // 接收广播信息，触发回调执行任务
    // 订阅到的是一个Json字符串

    auto reply = static_cast<redisReply *>(r);
    auto svr = static_cast<verify_internal *>(privdata);

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

void verify_internal::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto svr = static_cast<verify_internal *>(arg);

    if (!svr->_pub_context || !svr->_is_pub_connected)
    {
        svr->pubAttemptReconnect();
        return;
    }

    // 判断过期用户

    // 本地维护的在线用户续期
    svr->upload_record_item();
}

void verify_internal::Redis_Add_Temp_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    // 判断是否添加成功

    // 本地添加登录限制记录（用户数量）

    auth_limit active_info;
    active_info._connect_limit = 9999; // 匿名用户默认允许非常多的连接

    verify_internal::getInstance()->_unnamed_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, active_info));

    auth_reply Reply;
    Reply.type = AuthReply::OK;
    Reply.group_uid = "default";
    ctx->cb(nullptr, ctx->arg, &Reply);

    delete ctx;
}

void verify_internal::Redis_Verify_Access_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    if (!reply)
    {
        reject_auth_ctx(ctx, "redis_reply_missing");
        return;
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        continue_legacy_verify(ctx);
        return;
    }
    if (reply->type != REDIS_REPLY_STRING || !reply->str)
    {
        reject_auth_ctx(ctx, "unexpected_aacc_active_reply");
        return;
    }

    ctx->active_json = reply->str;
    auto active_info = new auth_limit;
    if (active_info->fromString(ctx->active_json) != 0)
    {
        delete active_info;
        reject_auth_ctx(ctx, "aacc_active_parse_failed", "User auth info invalid!");
        return;
    }
    ctx->active_info = active_info;

    const auto login_decision = navcaster::auth::AuthLoginService::evaluate_account(ctx->active_json, ctx->user_pwd, ctx->type, util_get_time_stamp());
    if (!login_decision.result.ok())
    {
        reject_auth_ctx(ctx, login_decision.result.message, login_decision.legacy_reply);
        return;
    }

    const std::string auth_type_name = navcaster::auth::AuthLoginService::auth_type_name(ctx->type);
    const bool require_mountpoint = ctx->type != AuthType::SOURCE;
    const auto runtime_validation = navcaster::core::validate_access_auth_index(
        login_decision.active_record,
        auth_type_name,
        ctx->runtime.mountpoint,
        util_get_time_stamp(),
        require_mountpoint);
    if (!runtime_validation.ok)
    {
        reject_auth_ctx(ctx, runtime_validation.reason);
        return;
    }

    ctx->active_info->_access_runtime_enabled = true;
    ctx->active_info->_owner_account_id = login_decision.active_record.value("owner_account_id", std::string{});
    ctx->active_info->_access_account_id = login_decision.active_record.value("access_account_id", std::string{});
    ctx->active_info->_access_username = login_decision.active_record.value("access_username", ctx->user_name);
    ctx->active_info->_access_kind = login_decision.active_record.value("access_kind", std::string{});
    ctx->active_info->_mount_point_group_id = login_decision.active_record.value("mount_point_group_id", std::string{});
    ctx->active_info->_balance_cents = json_i64_value(login_decision.active_record, "balance_cents", 0);
    ctx->active_info->_credit_limit_cents = json_i64_value(login_decision.active_record, "credit_limit_cents", 0);

    redisAsyncCommand(c,
                      Redis_Verify_Access_Record_Callback,
                      ctx,
                      "HGET %s %s",
                      navcaster::redis_keys::AACC_RECORD,
                      ctx->active_info->_access_account_id.c_str());
}

void verify_internal::Redis_Verify_Access_Record_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    std::string reason;
    if (!redis_reply_to_json_object(reply, ctx->access_record, reason))
    {
        reject_auth_ctx(ctx, "access_record_" + reason);
        return;
    }
    if (!json_status_active(ctx->access_record))
    {
        reject_auth_ctx(ctx, "access_account_disabled");
        return;
    }
    if (ctx->access_record.value("username", std::string{}) != ctx->user_name ||
        ctx->access_record.value("access_account_id", std::string{}) != ctx->active_info->_access_account_id)
    {
        reject_auth_ctx(ctx, "access_record_mismatch");
        return;
    }
    if (ctx->access_record.value("kind", std::string{}) != ctx->active_info->_access_kind)
    {
        reject_auth_ctx(ctx, "access_kind_mismatch");
        return;
    }
    const std::string owner_account_id = ctx->access_record.value("owner_account_id", std::string{});
    if (owner_account_id.empty())
    {
        reject_auth_ctx(ctx, "owner_account_required");
        return;
    }
    ctx->active_info->_owner_account_id = owner_account_id;
    ctx->active_info->_mount_point_group_id = ctx->access_record.value("mount_point_group_id", ctx->active_info->_mount_point_group_id);
    const int access_limit = ctx->access_record.value("concurrency_limit", ctx->active_info->_connect_limit);
    if (access_limit > 0)
    {
        ctx->active_info->_connect_limit = access_limit;
    }
    redisAsyncCommand(c,
                      Redis_Verify_Access_Owner_Callback,
                      ctx,
                      "HGET %s %s",
                      navcaster::redis_keys::ACC_RECORD,
                      owner_account_id.c_str());
}

void verify_internal::Redis_Verify_Access_Owner_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    std::string reason;
    if (!redis_reply_to_json_object(reply, ctx->owner_record, reason))
    {
        reject_auth_ctx(ctx, "owner_record_" + reason);
        return;
    }
    if (!json_status_active(ctx->owner_record))
    {
        reject_auth_ctx(ctx, "owner_account_disabled");
        return;
    }
    const std::string owner_role = ctx->owner_record.value("role", std::string{});
    const std::string access_kind = ctx->access_record.value("kind", std::string{});
    if (!((owner_role == "admin") ||
          (owner_role == "user" && access_kind == navcaster::core::ACCESS_RUNTIME_KIND_USER_CLIENT) ||
          (owner_role == "supplier" && access_kind == navcaster::core::ACCESS_RUNTIME_KIND_SUPPLIER_STATION)))
    {
        reject_auth_ctx(ctx, "access_kind_not_allowed_for_owner_role");
        return;
    }

    ctx->active_info->_balance_cents = json_i64_value(ctx->owner_record, "balance_cents", 0);
    ctx->active_info->_credit_limit_cents = json_i64_value(ctx->owner_record, "credit_limit_cents", 0);
    const int owner_limit = ctx->owner_record.value("concurrency_limit", 0);
    if (owner_limit > 0)
    {
        ctx->active_info->_connect_limit = std::min(ctx->active_info->_connect_limit, owner_limit);
    }
    if (ctx->active_info->_access_kind == navcaster::core::ACCESS_RUNTIME_KIND_USER_CLIENT &&
        ctx->active_info->_balance_cents + ctx->active_info->_credit_limit_cents < 0)
    {
        reject_auth_ctx(ctx, "balance_insufficient");
        return;
    }

    const std::string grant_key = append_key(navcaster::redis_keys::ACC_GROUP_PREFIX, ctx->active_info->_owner_account_id);
    redisAsyncCommand(c,
                      Redis_Verify_Access_Grant_Callback,
                      ctx,
                      "HGET %s %s",
                      grant_key.c_str(),
                      ctx->active_info->_mount_point_group_id.c_str());
}

void verify_internal::Redis_Verify_Access_Grant_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    std::string reason;
    if (!redis_reply_to_json_object(reply, ctx->grant_record, reason))
    {
        reject_auth_ctx(ctx, "group_grant_revoked");
        return;
    }
    if (!json_status_active(ctx->grant_record))
    {
        reject_auth_ctx(ctx, "group_grant_revoked");
        return;
    }
    redisAsyncCommand(c,
                      Redis_Verify_Access_Group_Callback,
                      ctx,
                      "HGET %s %s",
                      navcaster::redis_keys::MPGRP_RECORD,
                      ctx->active_info->_mount_point_group_id.c_str());
}

void verify_internal::Redis_Verify_Access_Group_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    std::string reason;
    if (!redis_reply_to_json_object(reply, ctx->group_record, reason))
    {
        reject_auth_ctx(ctx, "mount_point_group_disabled");
        return;
    }
    if (!json_status_active(ctx->group_record))
    {
        reject_auth_ctx(ctx, "mount_point_group_disabled");
        return;
    }
    ctx->active_info->_billing_multiplier = ctx->group_record.value("billing_multiplier", 1.0);

    if (ctx->runtime.mountpoint.empty() && ctx->type == AuthType::SOURCE)
    {
        Redis_Verify_Access_Accept(ctx);
        return;
    }
    const std::string member_key = append_key(navcaster::redis_keys::MPGRP_MEMBER_PREFIX, ctx->active_info->_mount_point_group_id);
    redisAsyncCommand(c,
                      Redis_Verify_Access_Member_Callback,
                      ctx,
                      "HGET %s %s",
                      member_key.c_str(),
                      ctx->runtime.mountpoint.c_str());
}

void verify_internal::Redis_Verify_Access_Member_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    std::string reason;
    if (!redis_reply_to_json_object(reply, ctx->member_record, reason))
    {
        reject_auth_ctx(ctx, "mountpoint_not_in_group");
        return;
    }
    if (!json_status_active(ctx->member_record))
    {
        reject_auth_ctx(ctx, "mountpoint_not_in_group");
        return;
    }
    redisAsyncCommand(c,
                      Redis_Verify_Access_Mount_Callback,
                      ctx,
                      "HGET %s %s",
                      navcaster::redis_keys::MOUNT_RECORD,
                      ctx->runtime.mountpoint.c_str());
}

void verify_internal::Redis_Verify_Access_Mount_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    if (reply && reply->type == REDIS_REPLY_NIL)
    {
        ctx->mount_record = json::object();
        Redis_Verify_Access_Accept(ctx);
        return;
    }

    std::string reason;
    if (!redis_reply_to_json_object(reply, ctx->mount_record, reason))
    {
        reject_auth_ctx(ctx, "mount_record_" + reason);
        return;
    }
    if (ctx->mount_record.contains("status") && !json_status_active(ctx->mount_record))
    {
        reject_auth_ctx(ctx, "mountpoint_disabled");
        return;
    }
    ctx->active_info->_hourly_price_cents = json_i64_value(ctx->mount_record, "hourly_price_cents", 0);
    if (ctx->active_info->_access_kind == navcaster::core::ACCESS_RUNTIME_KIND_USER_CLIENT)
    {
        const auto next_slice_cost = navcaster::core::calculate_runtime_cost_cents(
            60,
            ctx->active_info->_hourly_price_cents,
            ctx->active_info->_billing_multiplier);
        if (next_slice_cost > 0 && next_slice_cost > ctx->active_info->_balance_cents + ctx->active_info->_credit_limit_cents)
        {
            reject_auth_ctx(ctx, "balance_insufficient");
            return;
        }
    }
    Redis_Verify_Access_Accept(ctx);
}

void verify_internal::Redis_Verify_Access_Accept(auth_ctx *ctx)
{
    auto svr = verify_internal::getInstance();
    auto find = svr->_register_limit_map.find(ctx->user_name);
    if (find != svr->_register_limit_map.end())
    {
        svr->_register_limit_map.erase(find);
    }
    svr->_register_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, *ctx->active_info));

    auth_reply Reply;
    Reply.type = AuthReply::OK;
    Reply.group_uid = normalize_group_uid(ctx->active_info->_group);
    spdlog::info("[auth]: event=login_accepted operation=verify_access_account auth_type={} account={} owner_account_id={} access_account_id={} mountpoint={} group_uid={}",
                 navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                 ctx->user_name,
                 ctx->active_info->_owner_account_id,
                 ctx->active_info->_access_account_id,
                 ctx->runtime.mountpoint,
                 Reply.group_uid);
    ctx->cb(nullptr, ctx->arg, &Reply);
    delete_auth_ctx(ctx);
}

void verify_internal::Redis_Revalidate_Access_Runtime_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    (void)c;
    auto *ctx = static_cast<access_runtime_revalidation_ctx *>(privdata);
    if (!ctx)
    {
        return;
    }

    std::string reason;
    json active_record;
    if (!redis_reply_to_json_object(static_cast<redisReply *>(r), active_record, reason))
    {
        reason = reason == "not_found" ? "access_account_disabled" : "access_runtime_revalidation_" + reason;
    }
    else
    {
        const auto validation = navcaster::core::revalidate_access_runtime_session({
            active_record,
            ctx->auth_type,
            ctx->mountpoint,
            ctx->now,
            60,
            ctx->hourly_price_cents,
            ctx->billing_multiplier,
        });
        if (validation.ok)
        {
            delete ctx;
            return;
        }
        reason = validation.reason;
    }

    spdlog::warn("[auth]: event=access_runtime_revalidation_failed operation=revalidate_access_runtime_session account={} access_username={} connect_key={} reason={}",
                 ctx->user_name,
                 ctx->access_username,
                 ctx->connect_key,
                 reason);
    verify_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), ctx->connect_key.c_str(), AuthReply::ERR, reason.c_str());
    delete ctx;
}

void verify_internal::Redis_Verify_Callback(redisAsyncContext *c, void *r, void *privdata)
{
    auto reply = static_cast<redisReply *>(r);
    auto ctx = static_cast<auth_ctx *>(privdata);

    if (!reply)
    {
        reject_auth_ctx(ctx, "redis_reply_missing");
        return;
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        const auto login_decision = navcaster::auth::AuthLoginService::account_not_found();
        spdlog::warn("[auth]: event=login_rejected operation=verify_account auth_type={} account={} stage={} code={} reason={}",
                     navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                     ctx->user_name,
                     navcaster::auth::AuthLoginService::stage_name(login_decision.stage),
                     navcaster::core::core_error_code_name(login_decision.result.code),
                     login_decision.result.message);
        auth_reply Reply;
        Reply.type = AuthReply::ERR; // AUTH_REPLY_ERR;
        Reply.str = login_decision.legacy_reply.c_str();
        ctx->cb(nullptr, ctx->arg, &Reply);
        delete_auth_ctx(ctx);
        return;
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        spdlog::warn("[auth]: event=login_rejected operation=verify_account auth_type={} account={} stage=account_lookup code=redis_error reason=unexpected_reply_type",
                     navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                     ctx->user_name);
        reject_auth_ctx(ctx, "unexpected_legacy_active_reply");
        return;
    }

    // 解析查询到的信息

    // 判断密码是否一致

    // 将有效信息写入到本地记录中

    // 本地添加登录限制记录（用户数量）

    auth_limit active_info;
    if (active_info.fromString(reply->str) != 0)
    {
        spdlog::warn("[auth]: event=login_rejected operation=verify_account auth_type={} account={} stage=account_rejected code=parse_error reason=auth_limit_parse_failed",
                     navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                     ctx->user_name);
        auth_reply Reply;
        Reply.type = AuthReply::ERR;
        Reply.str = "User auth info invalid!";
        ctx->cb(nullptr, ctx->arg, &Reply);
        delete_auth_ctx(ctx);
        return;
    }

    const auto login_decision = navcaster::auth::AuthLoginService::evaluate_account(reply->str, ctx->user_pwd, ctx->type, util_get_time_stamp());
    if (!login_decision.result.ok())
    {
        spdlog::warn("[auth]: event=login_rejected operation=verify_account auth_type={} account={} stage={} code={} reason={}",
                     navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                     ctx->user_name,
                     navcaster::auth::AuthLoginService::stage_name(login_decision.stage),
                     navcaster::core::core_error_code_name(login_decision.result.code),
                     login_decision.result.message);
        auth_reply Reply;
        Reply.type = AuthReply::ERR;
        Reply.str = login_decision.legacy_reply.c_str();
        ctx->cb(nullptr, ctx->arg, &Reply);
        delete_auth_ctx(ctx);
        return;
    }

    // 为了避免已经写入记录，这里需要先删除原有的记录
    auto find = verify_internal::getInstance()->_register_limit_map.find(ctx->user_name);
    if (find != verify_internal::getInstance()->_register_limit_map.end())
    {
        verify_internal::getInstance()->_register_limit_map.erase(find);
    }
    verify_internal::getInstance()->_register_limit_map.insert(std::pair<std::string, auth_limit>(ctx->user_name, active_info));
    // 解析查询到的信息,存储到本地

    // 如果不存在，那么就不允许登录
    auth_reply Reply;
    Reply.type = AuthReply::OK; // AUTH_REPLY_ERR;
    Reply.group_uid = login_decision.group_uid;
    spdlog::info("[auth]: event=login_accepted operation=verify_account auth_type={} account={} group_uid={} stage={}",
                 navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                 ctx->user_name,
                 login_decision.group_uid,
                 navcaster::auth::AuthLoginService::stage_name(login_decision.stage));
    ctx->cb(nullptr, ctx->arg, &Reply);

    delete_auth_ctx(ctx);
}

void verify_internal::Redis_Add_Login_Callback(redisAsyncContext *c, void *r, void *privdata)
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

        auto limit_item = verify_internal::getInstance()->_register_limit_map.find(ctx->user_name);
        if (limit_item == verify_internal::getInstance()->_register_limit_map.end())
        {
            throw std::logic_error("Can't Find User Active Info"); // 找不到用户的登录限制信息
        }

        const bool online_protection = navcaster::auth::AuthLoginService::online_protection_enabled(
            ctx->type,
            current_login_options(*verify_internal::getInstance()));
        const auto decision = navcaster::auth::AuthLoginService::evaluate_record_limit(
            records,
            ctx->connect_key,
            limit_item->second._connect_limit,
            online_protection,
            ctx->user_name);
        _check = decision.current_allowed;
        spdlog::info("[auth]: event=record_limit_evaluated operation=add_login_record auth_type={} account={} connect_key={} online_protection={} current_allowed={} evicted_count={} code={} reason={}",
                     navcaster::auth::AuthLoginService::auth_type_name(ctx->type),
                     ctx->user_name,
                     ctx->connect_key,
                     online_protection ? "true" : "false",
                     decision.current_allowed ? "true" : "false",
                     decision.evicted_connect_keys.size(),
                     navcaster::core::core_error_code_name(decision.result.code),
                     decision.result.message);
        for (const auto &evicted_connect_key : decision.evicted_connect_keys)
        {
            spdlog::warn("[auth]: event=online_protection_evict operation=add_login_record account={} connect_key={} evicted_connect_key={} reason=record_limit_exceeded",
                         ctx->user_name,
                         ctx->connect_key,
                         evicted_connect_key);
            auto register_item = verify_internal::getInstance()->_register_map.find(ctx->user_name);
            if (register_item != verify_internal::getInstance()->_register_map.end())
            {
                auto evicted_item = register_item->second.find(evicted_connect_key);
                if (evicted_item != register_item->second.end())
                {
                    evicted_item->second.active_session_enabled = false;
                }
            }
            verify_internal::getInstance()->remove_active_session(ctx->user_name.c_str(), evicted_connect_key.c_str());
            verify_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), evicted_connect_key.c_str(), AuthReply::ERR, "User Connects Upper Limit , kick out this Connect!");
        }

        if (!_check) // 本条记录被踢出 不需要再发送OK的回调，  通过send_change_auth_status这个链路会使得这个连接接收到一个ERR的回调
        {
            throw std::logic_error(decision.legacy_reply); // 在返回的记录中不包含本条记录
        }

        // else 有1个或者多个连接，但是允许多个记录
        // 正常，返回一个成功回调
        auto register_item = verify_internal::getInstance()->_register_map.find(ctx->user_name);
        if (register_item != verify_internal::getInstance()->_register_map.end())
        {
            auto cb_item = register_item->second.find(ctx->connect_key);
            if (cb_item != register_item->second.end())
            {
                cb_item->second.group_uid = normalize_group_uid(limit_item->second._group);
                cb_item->second.access_runtime_enabled = limit_item->second._access_runtime_enabled;
                cb_item->second.owner_account_id = limit_item->second._owner_account_id;
                cb_item->second.access_account_id = limit_item->second._access_account_id;
                cb_item->second.access_username = limit_item->second._access_username;
                cb_item->second.access_kind = limit_item->second._access_kind;
                cb_item->second.mount_point_group_id = limit_item->second._mount_point_group_id;
                cb_item->second.balance_cents = limit_item->second._balance_cents;
                cb_item->second.credit_limit_cents = limit_item->second._credit_limit_cents;
                cb_item->second.hourly_price_cents = limit_item->second._hourly_price_cents;
                cb_item->second.billing_multiplier = limit_item->second._billing_multiplier;
                cb_item->second.billing_mode = limit_item->second._billing_mode;
                if (cb_item->second.runtime.mountpoint.empty() && ctx->has_runtime)
                {
                    cb_item->second.runtime = ctx->runtime;
                }
                const auto now = util_get_time_stamp();
                verify_internal::getInstance()->update_active_session(cb_item->second, now);
                if (verify_internal::getInstance()->write_access_runtime_login(cb_item->second, now) != REDIS_OK)
                {
                    throw std::logic_error("access runtime persist failed");
                }
                cb_item->second.active_session_enabled = true;
                spdlog::info("[auth]: event=login_record_accepted operation=add_login_record account={} connect_key={} group_uid={}",
                             ctx->user_name,
                             ctx->connect_key,
                             cb_item->second.group_uid);
            }
        }

        auth_reply Reply;
        Reply.type = AuthReply::OK;
        Reply.str = "";
        Reply.group_uid = normalize_group_uid(limit_item->second._group);
        ctx->cb(NULL, ctx->arg, &Reply);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[auth]: event=login_record_rejected operation=add_login_record account={} connect_key={} reason={}",
                     ctx->user_name,
                     ctx->connect_key,
                     e.what());
        auto register_item = verify_internal::getInstance()->_register_map.find(ctx->user_name);
        if (register_item != verify_internal::getInstance()->_register_map.end())
        {
            auto cb_item = register_item->second.find(ctx->connect_key);
            if (cb_item != register_item->second.end())
            {
                cb_item->second.active_session_enabled = false;
            }
        }
        verify_internal::getInstance()->remove_active_session(ctx->user_name.c_str(), ctx->connect_key.c_str());
        verify_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), ctx->connect_key.c_str(), AuthReply::ERR, e.what()); //
    }

    delete ctx;
}

void verify_internal::Redis_Add_Unname_Callback(redisAsyncContext *c, void *r, void *privdata)
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
        Reply.group_uid = "default";
        ctx->cb(NULL, ctx->arg, &Reply);
    }
    catch (const std::exception &e)
    {
        verify_internal::getInstance()->send_change_auth_status(ctx->user_name.c_str(), records.begin()->second.c_str(), AuthReply::ERR, e.what()); //
    }

    delete ctx;
}

void verify_internal::Redis_Update_Active_Callback(redisAsyncContext *c, void *r, void *privdata)
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
        spdlog::warn("[auth_broadcast_item:{}]: decode field failed, bytes={} what={}", __func__, str.size(), e.what());
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
    navcaster::account_schema::AccountAuthView view;
    std::string error;
    if (!navcaster::account_schema::parse_auth_view(str, view, &error))
    {
        spdlog::warn("[auth_limit:{}]: decode field failed, bytes={} what={}", __func__, str.size(), error);
        return 1;
    }

    try
    {
        json info = json::parse(str);
        _account = view.account;
        _password = view.password;
        _active = view.active == 1;
        _type = view.type;
        _date_limit = info.value("date_limit", static_cast<int64_t>(info.value("expire_time", 0.0)));
        _time_limit = info.value("time_limit", static_cast<int64_t>(info.value("available_seconds", 0)));
        _access = info.value("access", 0);
        _connect_limit = view.connection_limit;
        _group = view.group_uid;
        _expire = view.expire_time;
        _access_runtime_enabled = info.contains("access_account_id") || info.contains("owner_account_id");
        if (_access_runtime_enabled)
        {
            _owner_account_id = info.value("owner_account_id", std::string{});
            _access_account_id = info.value("access_account_id", std::string{});
            _access_username = info.value("access_username", view.account);
            _access_kind = info.value("access_kind", std::string{});
            _mount_point_group_id = info.value("mount_point_group_id", view.group_uid);
            _balance_cents = json_i64_value(info, "balance_cents", 0);
            _credit_limit_cents = json_i64_value(info, "credit_limit_cents", 0);
            _hourly_price_cents = json_i64_value(info, "hourly_price_cents", 0);
            _billing_multiplier = info.value("billing_multiplier", 1.0);
            _billing_mode = info.value("billing_mode", std::string(navcaster::core::ACCESS_RUNTIME_BILLING_MODE_PAYG));
        }
    }
    catch (const std::exception &e)
    {
        spdlog::warn("[auth_limit:{}]: decode field failed, bytes={} what={}", __func__, str.size(), e.what());
        return 1;
    }
    return 0;
}
