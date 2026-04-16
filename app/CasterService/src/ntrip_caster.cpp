#include "ntrip_caster.h"
#include "broadcast_msg.h"

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/thread.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/http.h>
#include "SysUsage.h"

#include <malloc.h> //试图解决linux下（glibc）内存不自动释放问题
// https://blog.csdn.net/kenanxiuji/article/details/48547285
// https://blog.csdn.net/u013259321/article/details/112031002

#define __class__ "ntrip_caster"

// int ntrip_caster::init_license_check()
// {
//     _license_check.gen_register_file();
//     _license_check.load_license_file();

//     if (_license_check.active())
//     {
//         spdlog::info("[{}:{}]:License has been verified", __class__, __func__);
//         spdlog::info("[{}:{}]:Current Online Client Limit: {} ,Current Online Server Limit: {}", __class__, __func__, _license_check.client_limit(), _license_check.server_limit());
//         spdlog::info("[{}:{}]:The remaining validity period of the license: {} day", __class__, __func__, (_license_check.expiration_time() - time(0)) / 86400);
//     }
//     else
//     {
//         spdlog::warn("[{}:{}]:License is not vaild, Current Online Limit: {} ,Current Online Limit: {}", __class__, __func__, _license_check.client_limit(), _license_check.server_limit());
//     }

//     if (time(0) > _license_check.expiration_time())
//     {
//         spdlog::warn("[{}:{}]: License expired, please replace it with a new valid license, program will reject all connections", __class__, __func__);
//         _compat_listener->disable_accept_new_connect();
//     }

//     _license_check_tv.tv_sec = 30;
//     _license_check_tv.tv_usec = 0;
//     _license_check_ev = event_new(_base, -1, EV_PERSIST, License_Check_Callback, this);
//     // 添加超时事件
//     event_add(_license_check_ev, &_license_check_tv);
//     return 0;
// }

// void ntrip_caster::License_Check_Callback(evutil_socket_t fd, short events, void *arg)
// {
//     auto *svr = static_cast<ntrip_caster *>(arg);

//     // 许可证是否无效，无效的许可证应用的是试用许可
//     //  检查当前是否已经激活
//     if (!svr->_license_check.active())
//     {
//         // 没有激活，写入一条日志，提示需要激活
//         spdlog::warn("[{}:{}]: This program does not have a valid license, and the service is restricted, Current Online Limit: {} ,Current Online Limit: {}", __class__, __func__, svr->_license_check.client_limit(), svr->_license_check.server_limit());
//     }

//     // 检查许可证有效期
//     if (time(0) > svr->_license_check.expiration_time())
//     {
//         // 许可证已经过期
//         spdlog::warn("[{}:{}]: License expired, please replace it with a new valid license, program will reject all connections", __class__, __func__);
//         svr->_compat_listener->disable_accept_new_connect();
//         return;
//     }

//     // 检查是否超限
//     //  检查当前用户和基站数量是否超限
//     if (svr->_client_map.size() > svr->_license_check.client_limit() || svr->_server_map.size() > svr->_license_check.server_limit())
//     {
//         spdlog::warn("[{}:{}]: The number of connections exceeds the license limit, Current Online Limit: {},Current Online Limit: {}", __class__, __func__, svr->_license_check.client_limit(), svr->_license_check.server_limit());
//         svr->_compat_listener->disable_accept_new_connect();
//         return;
//     }

//     svr->_compat_listener->enable_accept_new_connect();
// }

void ntrip_caster::Relay_Request_Callback(void *arg, const broadcast_msg &msg)
{
    auto svr = static_cast<ntrip_caster *>(arg);
    svr->process_relay(msg);
}

ntrip_caster::ntrip_caster()
{
    _base = event_base_new();
}

ntrip_caster::~ntrip_caster()
{
    event_base_free(_base);
}

ntrip_caster *ntrip_caster::getInstance()
{
    static ntrip_caster instance;
    return &instance;
}

int ntrip_caster::start()
{

    _refresh_state_interval = ntrip_config::getInstance()->_service_opt.refresh_state_interval();
    _output_state = ntrip_config::getInstance()->_service_opt.output_state();

    _timeout_tv.tv_sec = _refresh_state_interval;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);

    // 核心模块初始化（核心业务）
    compontent_init();

    // 附加模块初始化
    extra_init();

    // 添加超时事件
    event_add(_timeout_ev, &_timeout_tv);
    // 启动event_base处理线程
    start_server_thread();

    return 0;
}

int ntrip_caster::stop()
{
    // 删除定超时事件
    event_del(_timeout_ev);
    // 核心模块停止
    compontent_stop();
    // 关闭所有连接，关闭listener;
    event_base_loopexit(_base, NULL);

    return 0;
}

int ntrip_caster::update_state_info()
{
    // _state_info["connect_num"] = _connect_map.size();
    // _state_info["client_num"] = _client_map.size();
    // _state_info["server_num"] = _server_map.size();

    return 0;
}

int ntrip_caster::periodic_task()
{
    if (_output_state) // 输出状态信息
    {
        // spdlog::info("[Service Statistic]: Connection: {}, Server: {}, Client: {}, Pull: {}, Push: {}, Nearest: {}, Memory: {} BYTE.",
        //              _connect_map.size() + _pull_map.size() + _push_map.size(),
        //              _server_map.size(),
        //              _client_map.size(),
        //              _pull_map.size(),
        //              _push_map.size(),
        //              _near_map.size(),
        //              util_get_use_memory());
        spdlog::info("[CasterCore Status]: {}", CASTER::Get_Status());

        // double cpu = SysUsage::getInstance()->getProcessCPU();
        // size_t mem = SysUsage::getInstance()->getProcessMemory();

        // spdlog::info("[SysUsage Status]: CPU: {:.2f}%, MEM: {:.2f} MB", cpu, mem / 1024.0 / 1024.0);

        // 更新记录的状态信息
        update_state_info();
    }
#ifdef WIN32

#else
    malloc_trim(0); // 尝试归还、释放内存
#endif

    // 检测是否激活

    return 0;
}
int ntrip_caster::compontent_init()
{
    // 初始化请求处理队列
    _process_event = event_new(_base, -1, EV_PERSIST, Request_Process_Cb, this);
    QUEUE::Init(_process_event);

    // 用户验证模块
    AUTH::Init(ntrip_config::getInstance()->_auth_verify_opt, _base);

    // 初始化Caster数据分发核心：当前采用的是Redis，后续开发支持脱离redis运行
    CASTER::Init(ntrip_config::getInstance()->_caster_core_opt, _base);

    // 注册Relay请求回调
    CASTER::Relay_Register_Callback(Relay_Request_Callback, this);

    // 初始化connect_bev模块，管理连接相关的bufferevent
    connect_bev::getInstance()->init(_base);

    // 创建listener请求
    ntrip_listener::getInstance()->init(ntrip_config::getInstance()->_listener_opt, _base);
    ntrip_listener::getInstance()->start();

    return 0;
}

int ntrip_caster::compontent_stop()
{
    // _compat_listener->stop();
    // delete _compat_listener;

    // CASTER::Free();
    return 0;
}

int ntrip_caster::extra_init()
{
    // init_license_check();

    return 0;
}

int ntrip_caster::extra_stop()
{
    return 0;
}

int ntrip_caster::process_request(ConnectInfo req)
{
    try
    {
        // 根据请求的类型，执行对应的操作

        // spdlog::info("[{}:{}]: {} {}", __class__, __func__, ConnectType_Name(req.type()), OperateType_Name(req.operate()));
        switch (req.type())
        {
        // 一般ntrip请求-------------------------------------
        case CONNECT_TYPE_SOURCE:
            Sources.operateObject(req);
            break;
        case CONNECT_TYPE_SERVER:
            Servers.operateObject(req);
            break;
        case CONNECT_TYPE_CLIENT:
            Clients.operateObject(req);
            break;
        case CONNECT_TYPE_NEAREST:
            Nears.operateObject(req);
            break;
        case CONNECT_TYPE_PULL:
            Pulls.operateObject(req);
            break;
        case CONNECT_TYPE_PUSH:
            Pushs.operateObject(req);
            break;
        default:
            spdlog::warn("Not supported req type: {}:{}", ConnectType_Name(req.type()), OperateType_Name(req.operate()));
            break;
        }
    }
    catch (std::exception &e)
    {
        std::string dump_safe = "[[dump failed]]";
        try
        {
            dump_safe = req.DebugString();
        }
        catch (...)
        {
            // 忽略二次异常，保留默认提示
        }
        spdlog::warn("[{}:{}]: request_process error, from: [req_dump: {}] ,what: {}", __class__, __func__, dump_safe, e.what());
    }
    return 0;
}

int ntrip_caster::process_relay(const broadcast_msg &msg)
{
    ConnectInfo req;

    if (msg.type == caster::core::BOARDCAST_TYPE_PULL_OPERATE)
    {
        // 解析PullRecord
        caster::core::PullRecord record;
        if (!JsonToProto(msg.msg_str, record))
        {
            spdlog::warn("[{}:{}]: Failed to parse PullRecord from msg_str", __class__, __func__);
            return 1;
        }

        req.set_type(CONNECT_TYPE_PULL);
        req.set_connect_key(record.uid());
        req.set_mount_point(record.login_mpt());
        req.set_addr(record.target_ip());
        req.set_port(record.target_port());

        // 构造认证信息 (Base64编码 account:password)
        if (!record.target_account().empty())
        {
            std::string auth_raw = record.target_account() + ":" + record.target_password();
            req.set_ntrip_auth(auth_raw);
        }

        // 设置远端挂载点到http_host字段，用于NTRIP请求
        if (!record.target_mpt().empty())
        {
            req.set_mount_point(record.target_mpt());
        }

        switch (msg.operate)
        {
        case caster::core::BOARDCAST_OPERATR_ACTIVE:
            req.set_operate(OPERATE_TYPE_CREATE);
            break;
        case caster::core::BOARDCAST_OPERATR_INACTIVE:
            req.set_operate(OPERATE_TYPE_DESTORY);
            break;
        case caster::core::BOARDCAST_OPERATE_UPDATE:
            req.set_operate(OPERATE_TYPE_UPDATE);
            break;
        default:
            return 1;
        }

        Pulls.operateObject(req);
    }
    else if (msg.type == caster::core::BOARDCAST_TYPE_RUSH_OPERATE)
    {
        // 解析PushRecord
        caster::core::PushRecord record;
        if (!JsonToProto(msg.msg_str, record))
        {
            spdlog::warn("[{}:{}]: Failed to parse PushRecord from msg_str", __class__, __func__);
            return 1;
        }

        req.set_type(CONNECT_TYPE_PUSH);
        req.set_connect_key(record.uid());
        req.set_mount_point(record.login_mpt());
        req.set_addr(record.target_ip());
        req.set_port(record.target_port());

        // 构造认证信息
        if (!record.target_account().empty())
        {
            std::string auth_raw = record.target_account() + ":" + record.target_password();
            req.set_ntrip_auth(auth_raw);
        }

        // 设置远端挂载点
        if (!record.target_mpt().empty())
        {
            req.set_mount_point(record.target_mpt());
        }

        switch (msg.operate)
        {
        case caster::core::BOARDCAST_OPERATR_ACTIVE:
            req.set_operate(OPERATE_TYPE_CREATE);
            break;
        case caster::core::BOARDCAST_OPERATR_INACTIVE:
            req.set_operate(OPERATE_TYPE_DESTORY);
            break;
        case caster::core::BOARDCAST_OPERATE_UPDATE:
            req.set_operate(OPERATE_TYPE_UPDATE);
            break;
        default:
            return 1;
        }

        Pushs.operateObject(req);
    }
    else
    {
        spdlog::warn("[{}:{}]: Unsupported relay broadcast type: {}", __class__, __func__, static_cast<int>(msg.type));
        return 1;
    }

    return 0;
}

// int ntrip_caster::build_relay_request(CasterBroadcastType type, std::string req_str)
// {
//     return 0;
// }

// int ntrip_caster::close_unsuccess_req_connect(json req)
// {
//     std::string Connect_Key = req["connect_key"];
//     int reqtype = req["req_type"];
//     std::string mount = req["mount_point"];

//     auto item = _connect_map.find(Connect_Key);
//     if (item == _connect_map.end())
//     {
//         spdlog::warn("[{}]:can't find need close connect. mount: [{}] ,connect key: [{}], req type: [{}]", __class__, mount, Connect_Key, reqtype);
//         return 1;
//     }
//     bufferevent *bev = item->second;

//     int fd = bufferevent_getfd(bev);
//     std::string ip = util_get_user_ip(fd);
//     int port = util_get_user_port(fd);

//     bufferevent_free(bev);
//     _connect_map.erase(item);

//     return 0;
// }

int ntrip_caster::start_server_thread()
{
    event_base_thread(_base);
    return 0;
}

void *ntrip_caster::event_base_thread(void *arg)
{
    event_base *base = static_cast<event_base *>(arg);
    evthread_make_base_notifiable(base);

    spdlog::info("Server is runing...");
    event_base_dispatch(base);

    spdlog::warn("Server is stop!"); // 不应当主动发生
    return nullptr;
}

void ntrip_caster::Request_Process_Cb(evutil_socket_t fd, short what, void *arg)
{
    ntrip_caster *svr = static_cast<ntrip_caster *>(arg);
    if (QUEUE::Not_Null())
    {
        auto req = QUEUE::Pop();
        svr->process_request(req);
    }

    if (QUEUE::Not_Null())
    {
        QUEUE::Active();
    }
}

void ntrip_caster::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *svr = static_cast<ntrip_caster *>(arg);
    svr->periodic_task();
}
