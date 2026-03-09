#include "ntrip_caster.h"

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

int ntrip_caster::init_license_check()
{
    _license_check.gen_register_file();
    _license_check.load_license_file();

    if (_license_check.active())
    {
        spdlog::info("[{}:{}]:License has been verified", __class__, __func__);
        spdlog::info("[{}:{}]:Current Online Client Limit: {} ,Current Online Server Limit: {}", __class__, __func__, _license_check.client_limit(), _license_check.server_limit());
        spdlog::info("[{}:{}]:The remaining validity period of the license: {} day", __class__, __func__, (_license_check.expiration_time() - time(0)) / 86400);
    }
    else
    {
        spdlog::warn("[{}:{}]:License is not vaild, Current Online Limit: {} ,Current Online Limit: {}", __class__, __func__, _license_check.client_limit(), _license_check.server_limit());
    }

    if (time(0) > _license_check.expiration_time())
    {
        spdlog::warn("[{}:{}]: License expired, please replace it with a new valid license, program will reject all connections", __class__, __func__);
        _compat_listener->disable_accept_new_connect();
    }

    _license_check_tv.tv_sec = 30;
    _license_check_tv.tv_usec = 0;
    _license_check_ev = event_new(_base, -1, EV_PERSIST, License_Check_Callback, this);
    // 添加超时事件
    event_add(_license_check_ev, &_license_check_tv);
    return 0;
}

void ntrip_caster::License_Check_Callback(evutil_socket_t fd, short events, void *arg)
{
    auto *svr = static_cast<ntrip_caster *>(arg);

    // 许可证是否无效，无效的许可证应用的是试用许可
    //  检查当前是否已经激活
    if (!svr->_license_check.active())
    {
        // 没有激活，写入一条日志，提示需要激活
        spdlog::warn("[{}:{}]: This program does not have a valid license, and the service is restricted, Current Online Limit: {} ,Current Online Limit: {}", __class__, __func__, svr->_license_check.client_limit(), svr->_license_check.server_limit());
    }

    // 检查许可证有效期
    if (time(0) > svr->_license_check.expiration_time())
    {
        // 许可证已经过期
        spdlog::warn("[{}:{}]: License expired, please replace it with a new valid license, program will reject all connections", __class__, __func__);
        svr->_compat_listener->disable_accept_new_connect();
        return;
    }

    // 检查是否超限
    //  检查当前用户和基站数量是否超限
    if (svr->_client_map.size() > svr->_license_check.client_limit() || svr->_server_map.size() > svr->_license_check.server_limit())
    {
        spdlog::warn("[{}:{}]: The number of connections exceeds the license limit, Current Online Limit: {},Current Online Limit: {}", __class__, __func__, svr->_license_check.client_limit(), svr->_license_check.server_limit());
        svr->_compat_listener->disable_accept_new_connect();
        return;
    }

    svr->_compat_listener->enable_accept_new_connect();
}

void ntrip_caster::Relay_Request_Callback(void *arg, CasterBroadcastType type, std::string req_str)
{
    if (type == CasterBroadcastType::RELAY_PULL_ACTIVE)
    {
        // 创建一个请求，添加到队列中去
        json req = json::parse(req_str);
        req["req_type"] = REQUEST_RELAY_PULL;
        QUEUE::Push(req);
    }
    if (type == CasterBroadcastType::RELAY_PULL_INACTIVE)
    {
        // 创建一个请求，添加到队列中去
        json req = json::parse(req_str);
        req["req_type"] = STOP_RELAY_PULL;
        QUEUE::Push(req);
    }
    if (type == CasterBroadcastType::RELAY_PULL_UPDATE)
    {
        // 创建一个请求，添加到队列中去
        json req = json::parse(req_str);
        req["req_type"] = UPDATE_RELAY_PULL;
        QUEUE::Push(req);
    }
    if (type == CasterBroadcastType::RELAY_PUSH_ACTIVE)
    {
        // 创建一个请求，添加到队列中去
        json req = json::parse(req_str);
        req["req_type"] = REQUEST_RELAY_PUSH;
        QUEUE::Push(req);
    }
    if (type == CasterBroadcastType::RELAY_PUSH_INACTIVE)
    {
        // 创建一个请求，添加到队列中去
        json req = json::parse(req_str);
        req["req_type"] = STOP_RELAY_PUSH;
        QUEUE::Push(req);
    }
    if (type == CasterBroadcastType::RELAY_PUSH_UPDATE)
    {
        // 创建一个请求，添加到队列中去
        json req = json::parse(req_str);
        req["req_type"] = UPDATE_RELAY_PUSH;
        QUEUE::Push(req);
    }
}

ntrip_caster::ntrip_caster()
{
}

ntrip_caster::~ntrip_caster()
{
    event_base_free(_base);
}

ntrip_caster *ntrip_caster::getInstance()
{
    static ntrip_caster *instance = new ntrip_caster();
    return instance;
}

int ntrip_caster::init(json cfg)
{
    std::string dump_conf = cfg.dump(4);
    spdlog::debug("load conf info:\n{}", dump_conf);

    _service_setting = cfg["Service_Setting"];
    _caster_core_setting = cfg["Core_Setting"];
    _auth_verify_setting = cfg["Auth_Setting"];

    _common_setting = _service_setting["Common_Setting"];

    _listener_setting = _service_setting["Ntrip_Listener"];
    _client_setting = _service_setting["Client_Setting"];
    _server_setting = _service_setting["Server_Setting"];

    _refresh_state_interval = _common_setting["Refresh_State_Interval"];
    _output_state = _common_setting["Output_State"];

    _base = event_base_new();

    _timeout_tv.tv_sec = _refresh_state_interval;
    _timeout_tv.tv_usec = 0;
    _timeout_ev = event_new(_base, -1, EV_PERSIST, TimeoutCallback, this);

    return 0;
}

int ntrip_caster::start()
{
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
    _state_info["connect_num"] = _connect_map.size();
    _state_info["client_num"] = _client_map.size();
    _state_info["server_num"] = _server_map.size();

    return 0;
}

int ntrip_caster::periodic_task()
{
    if (_output_state) // 输出状态信息
    {
        spdlog::info("[Service Statistic]: Connection: {}, Server: {}, Client: {}, Pull: {}, Push: {}, Nearest: {}, Memory: {} BYTE.",
                     _connect_map.size() + _pull_map.size() + _push_map.size(),
                     _server_map.size(),
                     _client_map.size(),
                     _pull_map.size(),
                     _push_map.size(),
                     _near_map.size(),
                     util_get_use_memory());
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
    AUTH::Init(_auth_verify_setting.dump().c_str(), _base);

    // 初始化Caster数据分发核心：当前采用的是Redis，后续开发支持脱离redis运行
    CASTER::Init(_caster_core_setting.dump().c_str(), _base);

    // 注册Relay请求回调
    CASTER::Relay_Register_Callback(Relay_Request_Callback, this);

    // 创建listener请求
    _compat_listener = new ntrip_compat_listener(_listener_setting, _base, &_connect_map);
    _compat_listener->start();

    return 0;
}

int ntrip_caster::compontent_stop()
{
    _compat_listener->stop();
    delete _compat_listener;

    CASTER::Free();
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

int ntrip_caster::request_process(std::shared_ptr<ReqBase> req)
{
    try
    {
        // 根据请求的类型，执行对应的操作
        switch (req.type)
        {
        // 一般ntrip请求-------------------------------------
        case CONNECT_TYPE_SOURCE:
            operate_source_ntrip(req);
            break;
        case CONNECT_TYPE_SERVER:
            operate_server_ntrip(req);
            break;
        case CONNECT_TYPE_CLIENT:
            operate_client_ntrip(req);
            break;
        case CONNECT_TYPE_NEAREST:
            operate_client_near(req);
            break;
        case CONNECT_TYPE_PROXY:
            operate_client_proxy(req);
            break;
        case CONNECT_TYPE_ALIAS:
            operate_client_alias(req);
            break;
        default:
            spdlog::warn("undefined req_type: {}", req.type());
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

int ntrip_caster::operate_client_ntrip(std::shared_ptr<ReqBase> req)
{
    switch (req.operate())
    {
    case OPERATE_TYPE_CREATE:
    {
        std::string connect_key = req.connect_key();
        auto con = _connect_map.find(connect_key);
        if (con == _connect_map.end())
        {
            spdlog::warn("[{}:{}]: Create_Ntrip_Client fail, con not in connect_map", __class__, __func__);
            return 1;
        }

        auto item = std::make_shared<client_ntrip>(req, con->second);
        _client_map.insert(std::pair<std::string, std::shared_ptr<client_ntrip>>(connect_key, item));
        item->start();
    }
    break;
    case OPERATE_TYPE_DESTORY:
        /* code */
        break;
    default:
        break;
    }

    return 0;
}

int ntrip_caster::operate_server_ntrip(std::shared_ptr<ReqBase> req)
{
    switch (req.operate())
    {
    case OPERATE_TYPE_CREATE:
        /* code */
        break;
    case OPERATE_TYPE_DESTORY:
        /* code */
        break;
    default:
        break;
    }

    return 0;
}

int ntrip_caster::operate_source_ntrip(std::shared_ptr<ReqBase> req)
{
    auto info = std::dynamic_pointer_cast<CommonReq>(req);
    if (!info)
    {
        spdlog::warn("[{}:{}]: req is not CommonReq", __class__, __func__);
        return 1;
    }

    switch (req.operate)
    {
    case OPERATE_TYPE_CREATE:
        /* code */
        break;
    case OPERATE_TYPE_DESTORY:
        /* code */
        break;
    default:
        break;
    }

    return 0;
}

int ntrip_caster::operate_client_near(std::shared_ptr<ReqBase> req)
{
    switch (req.operate())
    {
    case OPERATE_TYPE_CREATE:
        /* code */
        break;
    case OPERATE_TYPE_DESTORY:
        /* code */
        break;
    default:
        break;
    }

    return 0;
}

int ntrip_caster::operate_client_proxy(std::shared_ptr<ReqBase> req)
{
    switch (req.operate())
    {
    case OPERATE_TYPE_CREATE:
        /* code */
        break;
    case OPERATE_TYPE_DESTORY:
        /* code */
        break;
    default:
        break;
    }

    return 0;
}

int ntrip_caster::operate_client_alias(std::shared_ptr<ReqBase> req)
{
    switch (req.operate())
    {
    case OPERATE_TYPE_CREATE:
        /* code */
        break;
    case OPERATE_TYPE_DESTORY:
        /* code */
        break;
    default:
        break;
    }

    return 0;
}

int ntrip_caster::create_source_ntrip(std::shared_ptr<ReqBase> req)
{
    std::string connect_key = req["connect_key"];
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        spdlog::warn("[{}:{}]: Create Source_Ntrip fail, con not in connect_map,connect_key: {}", __class__, __func__, connect_key);
        return 1;
    }

    auto *source = new source_ntrip(req, con->second);
    _source_map.insert(std::pair<std::string, source_ntrip *>(connect_key, source));
    source->start();

    return 0;
}

int ntrip_caster::close_source_ntrip(std::shared_ptr<ReqBase> req)
{
    json origin_req = req["origin_req"];
    std::string connect_key = origin_req["connect_key"];

    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
    }
    else
    {
        _connect_map.erase(con);
    }

    auto obj = _source_map.find(connect_key);
    if (obj == _source_map.end())
    {
    }
    else
    {
        delete obj->second;
        _source_map.erase(obj);
    }
    return 0;
}

int ntrip_caster::create_client_ntrip(json req)
{
    std::string connect_key = req["connect_key"];
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        spdlog::warn("[{}:{}]: Create_Ntrip_Client fail, con not in connect_map", __class__, __func__);
        return 1;
    }
    req["Settings"] = _client_setting;
    client_ntrip *ntripc = new client_ntrip(req, con->second);
    _client_map.insert(std::pair<std::string, client_ntrip *>(connect_key, ntripc));
    ntripc->start();

    return 0;
}

int ntrip_caster::close_client_ntrip(json req)
{
    json origin_req = req["origin_req"];
    std::string connect_key = origin_req["connect_key"];
    std::string mount_point = origin_req["mount_point"];
    int req_type = origin_req["req_type"];
    auto con = _connect_map.find(connect_key);

    if (con == _connect_map.end())
    {
    }
    else
    {
        _connect_map.erase(con);
    }

    auto obj = _client_map.find(connect_key);
    if (obj == _client_map.end())
    {
    }
    else
    {
        delete obj->second;
        _client_map.erase(obj);
    }
    return 0;
}

int ntrip_caster::create_client_near(json req)
{
    std::string connect_key = req["connect_key"];
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        spdlog::warn("[{}:{}]: Create_Ntrip_Client fail, con not in connect_map", __class__, __func__);
        return 1;
    }
    req["Settings"] = _client_setting;
    client_near *ntripc = new client_near(req, con->second);
    _near_map.insert(std::pair<std::string, client_near *>(connect_key, ntripc));
    ntripc->start();

    return 0;
}

int ntrip_caster::close_client_near(json req)
{
    json origin_req = req["origin_req"];
    std::string connect_key = origin_req["connect_key"];
    std::string mount_point = origin_req["mount_point"];
    int req_type = origin_req["req_type"];
    auto con = _connect_map.find(connect_key);

    if (con == _connect_map.end())
    {
    }
    else
    {
        _connect_map.erase(con);
    }

    auto obj = _near_map.find(connect_key);
    if (obj == _near_map.end())
    {
    }
    else
    {
        delete obj->second;
        _near_map.erase(obj);
    }

    return 0;
}

int ntrip_caster::create_relay_pull(json req)
{
    std::string UID = req["UID"];

    auto item = _pull_map.find(UID);
    if (item != _pull_map.end())
    {
        return 1; // 已经存在
    }
    relay_pull *obj = new relay_pull(req, _base);
    _pull_map.insert(std::pair<std::string, relay_pull *>(UID, obj));
    obj->start();
    return 0;
}

int ntrip_caster::stop_relay_pull(json req)
{
    // 找到已经运行的实例
    auto item = _pull_map.find(req["UID"]);
    if (item == _pull_map.end())
    {
        return 1; // 不存在
    }
    // 停止服务
    item->second->stop();
    return 0;
}

int ntrip_caster::update_relay_pull(json req)
{
    return 0;
}

int ntrip_caster::close_relay_pull(json req)
{
    json origin_req = req["origin_req"];
    auto obj = _pull_map.find(origin_req["login_mpt"]);
    if (obj == _pull_map.end())
    {
    }
    else
    {
        delete obj->second;
        _pull_map.erase(obj);
    }
    return 0;
}

int ntrip_caster::create_relay_push(json req)
{
    std::string UID = req["UID"];

    auto item = _push_map.find(UID);
    if (item != _push_map.end())
    {
        return 1; // 已经存在
    }
    relay_push *obj = new relay_push(req, _base);
    _push_map.insert(std::pair<std::string, relay_push *>(UID, obj));
    obj->start();
    return 0;
}

int ntrip_caster::stop_relay_push(json req)
{
    // 找到已经运行的实例
    auto item = _push_map.find(req["UID"]);
    if (item == _push_map.end())
    {
        return 1; // 不存在
    }
    // 停止服务
    item->second->stop();
    return 0;
}

int ntrip_caster::update_relay_push(json req)
{
    return 0;
}

int ntrip_caster::close_relay_push(json req)
{
    json origin_req = req["origin_req"];
    auto obj = _push_map.find(origin_req["login_mpt"]);
    if (obj == _push_map.end())
    {
    }
    else
    {
        delete obj->second;
        _push_map.erase(obj);
    }
    return 0;
}

int ntrip_caster::create_server_ntrip(json req)
{
    std::string connect_key = req["connect_key"];

    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
        spdlog::warn("[{}:{}]: Create_Ntrip_Server fail, con not in connect_map", __class__, __func__);
        return 1; // 找不到连接
    }
    req["Settings"] = _server_setting;
    server_ntrip *ntrips = new server_ntrip(req, con->second);
    // 加入挂载点表中
    _server_map.insert(std::pair<std::string, server_ntrip *>(connect_key, ntrips));

    // 一切准备就绪，启动server
    ntrips->start();
    return 0;
}

int ntrip_caster::close_server_ntrip(json req)
{
    json origin_req = req["origin_req"];
    std::string connect_key = origin_req["connect_key"];
    std::string mount_point = origin_req["mount_point"];
    auto con = _connect_map.find(connect_key);
    if (con == _connect_map.end())
    {
    }
    else
    {
        _connect_map.erase(con);
    }

    auto obj = _server_map.find(connect_key);
    if (obj == _server_map.end())
    {
    }
    else
    {
        delete obj->second;
        _server_map.erase(obj);
    }

    return 0;
}

int ntrip_caster::close_unsuccess_req_connect(json req)
{
    std::string Connect_Key = req["connect_key"];
    int reqtype = req["req_type"];
    std::string mount = req["mount_point"];

    auto item = _connect_map.find(Connect_Key);
    if (item == _connect_map.end())
    {
        spdlog::warn("[{}]:can't find need close connect. mount: [{}] ,connect key: [{}], req type: [{}]", __class__, mount, Connect_Key, reqtype);
        return 1;
    }
    bufferevent *bev = item->second;

    int fd = bufferevent_getfd(bev);
    std::string ip = util_get_user_ip(fd);
    int port = util_get_user_port(fd);

    bufferevent_free(bev);
    _connect_map.erase(item);

    return 0;
}

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
        svr->request_process(req);
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
