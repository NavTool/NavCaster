#pragma once

#include <event2/util.h>
#include <event2/event.h>
#include <event2/http.h>

#include <queue>
#include <list>
#include <mutex>
#include <memory>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

enum ConnectType
{
    CONNECT_TYPE_UNKNOWN = 0, // 未知
    CONNECT_TYPE_SOURCE = 1,  // 源列表
    CONNECT_TYPE_SERVER = 2,  // NtripServer
    CONNECT_TYPE_CLIENT = 3,  // NtripClient
    CONNECT_TYPE_NEAREST = 4, // 最近点
    CONNECT_TYPE_PROXY = 5,   // Proxy
    CONNECT_TYPE_ALIAS = 6,   // Alias
    CONNECT_TYPE_PULL = 7,    // Pull
    CONNECT_TYPE_PUSH = 8,    // Push
    CONNECT_TYPE_GRID = 9,    // Grid
    CONNECT_TYPE_VRS = 10     // 虚拟参考站
};

enum OperateType
{
    OPERATE_TYPE_UNKNOWN = 0, // 未知
    OPERATE_TYPE_CREATE = 1,  // 创建对象
    OPERATE_TYPE_DESTORY = 2 // 释放对象
                              // OPERATE_TYPE_PAUSE   = 3; // 暂停
                              // OPERATE_TYPE_UPDATE  = 4; // 更新

};

class ReqBase
{
public:
    ConnectType type;    // 登录类型  根据这个类型来执行创建和释放函数
    OperateType operate; // 操作类型  创建，销毁，停止，更新
};

class CommonReq : public ReqBase
{
public:
    // TCP KEY
    std::string connect_key;

    std::string mount_point;
    std::string mount_para;

    std::string user_base64;
    std::string user_name;
    std::string user_pwd;

    std::string http_host;     // Host
    std::string http_chunked;  // Transfer-Encoding
    std::string user_agent;    // User-Agent / Source-Agent
    std::string mount_info;    // STR
    std::string ntrip_version; // Ntrip-Version
    std::string ntrip_gga;     // Ntrip-GGA
    std::string ntrip_auth;    // Authorization
};

class RelayReq : public ReqBase
{
};

namespace QUEUE
{
    int Init(event *process_event);
    int Free();

    int Push(std::shared_ptr<ReqBase> req);
    std::shared_ptr<ReqBase> Pop();

    bool Active();
    bool Not_Null();

} // namespace QUEUE
