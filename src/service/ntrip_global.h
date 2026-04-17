/*
    全局生效的宏定义，宏定义应避免简单，写的详细一些
*/
#pragma once

// 工程配置
#include "version.h"
#include "Caster_Core.h"
#include "Auth_Verify.h"

#include "connect_bev.h"

#include "google/protobuf/json/json.h"
#include "proto_json.h"
#include "service/ListenerOpt.pb.h"
#include "service/ServiceOpt.pb.h"
#include "service/AuthVerifyOpt.pb.h"
#include "service/CasterCoreOpt.pb.h"

#include "service/ConnectInfo.pb.h"
#include "service/CarrierOpt.pb.h"

using namespace caster::service;

// #define SOFTWARE_NAME "KORO_Caster"
// #define SOFTWARE_VERSION "0.0.2"

// 挂载点类型
enum class MountType : int {
    Common  = 1,
    Nearest = 2,
    Relay   = 3,
    Virtual = 4,
};

// 开关
enum class SwitchType : int {
    EnableSysNtripRelay  = 101,
    DisableSysNtripRelay = 102,
    EnableTrdNtripRelay  = 103,
    DisableTrdNtripRelay = 104,
    EnableHttpServer     = 105,
    DisableHttpServer    = 106,
};

// 核心部件操作请求

// 一般ntrip请求
enum class NtripRequestType : int {
    SourceLogin  = 301,
    CloseSource  = 302,
    ClientLogin  = 303,
    NearestLogin = 304,
    CloseNearest = 305,
    CloseClient  = 307,
    ServerLogin  = 308,
    CloseServer  = 309,
    AliasLogin   = 304, // 与 NearestLogin 共用
    CloseAlias   = 305, // 与 CloseNearest 共用
};

// RELAY请求
enum class RelayRequestType : int {
    PullRequest = 601,
    PullStop    = 602,
    PullUpdate  = 603,
    PullClose   = 604,
    PushRequest = 701,
    PushStop    = 702,
    PushUpdate  = 703,
    PushClose   = 704,
};

// relay服务相关
enum class RelayServiceType : int {
    CreateServer          = 402,
    CloseServer           = 403,
    AddMountToListener    = 404,
    CloseReqConnect       = 405,
    AddMountToSourceList  = 406,
};

// 验证相关
enum class VerifyCloseType : int {
    MountNotOnline         = 501,
    MountAlreadyOnline     = 502,
    NoIdleRelayAccount     = 503,
    CreateRelayConnectFail = 504,
    AlreadySentSourceList  = 505,
};

template <typename T>
class Carrier
{
    std::unordered_map<std::string, std::shared_ptr<T>> m_obj_map;

public:
    int createObject(ConnectInfo req)
    {
        auto obj = std::make_shared<T>(req);
        auto existing = m_obj_map.find(req.connect_key());
        if (existing != m_obj_map.end())
        {
            existing->second->stop(); // properly stop old task
            m_obj_map.erase(existing);
        }
        m_obj_map.insert(std::pair(req.connect_key(), obj));
        obj->init();
        obj->start();
        return 0;
    }

    int destroyObject(ConnectInfo req)
    {
        auto iter = m_obj_map.find(req.connect_key());
        if (iter != m_obj_map.end())
        {
            iter->second->stop();
            m_obj_map.erase(iter);
        }
        return 0;
    }

    int updateObject(ConnectInfo req)
    {
        return 0;
    }

    int operateObject(ConnectInfo req)
    {
        switch (req.operate())
        {
        case OPERATE_TYPE_CREATE:
            return createObject(req);
        case OPERATE_TYPE_DESTROY:
            return destroyObject(req);
        case OPERATE_TYPE_PAUSE:
            /* code */
            break;
        case OPERATE_TYPE_UPDATE:
            return updateObject(req);
        }

        return 0;
    }
};

// // 时间
// #define EVENT_TIMEOUT_SEC 5

// // 大小相关宏定义
// #define VERIFY_INFO_H_MAX_CHAR 128

// #define BUFFEVENT_READ_DATA_SIZE 4096

// #define MAX_CAHR 128

// rtklib的

// #define TINTACT 200              /* period for stream active (ms) */
// #define SERIBUFFSIZE 4096        /* serial buffer size (bytes) */
// #define TIMETAGH_LEN 64          /* time tag file header length */
// #define MAXCLI 32                /* max client connection for tcp svr */
// #define MAXSTATMSG 32            /* max length of status message */
// #define DEFAULT_MEMBUF_SIZE 4096 /* default memory buffer size (bytes) */

// #define NTRIP_AGENT "RTKLIB/" VER_RTKLIB
// #define NTRIP_CLI_PORT 2101                       /* default ntrip-client connection port */
// #define NTRIP_SVR_PORT 80                         /* default ntrip-server connection port */
// #define NTRIP_MAXRSP 32768                        /* max size of ntrip response */
// #define NTRIP_MAXSTR 256                          /* max length of mountpoint string */
// #define NTRIP_RSP_OK_CLI "ICY 200 OK\r\n"         /* ntrip response: client */
// #define NTRIP_RSP_OK_SVR "OK\r\n"                 /* ntrip response: server */
// #define NTRIP_RSP_SRCTBL "SOURCETABLE 200 OK\r\n" /* ntrip response: source table */
// #define NTRIP_RSP_TBLEND "ENDSOURCETABLE"
// #define NTRIP_RSP_HTTP "HTTP/"  /* ntrip response: http */
// #define NTRIP_RSP_ERROR "ERROR" /* ntrip response: error */
// #define NTRIP_RSP_UNAUTH "HTTP/1.0 401 Unauthorized\r\n"
// #define NTRIP_RSP_ERR_PWD "ERROR - Bad Pasword\r\n"
// #define NTRIP_RSP_ERR_MNTP "ERROR - Bad Mountpoint\r\n"
