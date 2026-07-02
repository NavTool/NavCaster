#include "Caster_Core.h"
#include <spdlog/spdlog.h>
#include <cstring>

#include "core_callback_result.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

// #include "caster_core_internal.h"
#include "caster_internal.h"

namespace
{
using navcaster::core::CoreErrorCode;
using navcaster::core::CoreResult;

bool missing_text(const char *value)
{
    return value == nullptr || value[0] == '\0';
}

const char *safe_group_uid(const char *group_uid)
{
    return missing_text(group_uid) ? "default" : group_uid;
}

const char *safe_optional_text(const char *value)
{
    return value ? value : "";
}

const char *caster_register_type_name(CasterRegisterType type)
{
    switch (type)
    {
    case CasterRegisterType::SERVER:
        return "SERVER";
    case CasterRegisterType::CLIENT:
        return "CLIENT";
    case CasterRegisterType::NEAREST:
        return "NEAREST";
    case CasterRegisterType::ALIAS:
        return "ALIAS";
    case CasterRegisterType::PULL:
        return "PULL";
    case CasterRegisterType::PUSH:
        return "PUSH";
    case CasterRegisterType::UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

CoreResult missing_argument_result(const char *operation, const char *subject)
{
    return CoreResult::failure(CoreErrorCode::InvalidArgument,
                               operation,
                               "missing required argument")
        .with_subject(subject);
}

CoreResult unsupported_type_result(const char *operation, CasterRegisterType type)
{
    return CoreResult::failure(CoreErrorCode::InvalidArgument,
                               operation,
                               "unsupported register type")
        .with_subject(caster_register_type_name(type));
}

int finish_core_facade_failure(const CoreResult &result, CasterCallback cb = nullptr, void *arg = nullptr, int failure_value = 1)
{
    spdlog::warn("[CASTER facade]: {}", result.summary());
    navcaster::core::invoke_caster_callback(cb, arg, result);
    return navcaster::core::to_legacy_int(result, failure_value);
}

CoreResult require_connect_key(const char *operation, const char *connect_key)
{
    if (missing_text(connect_key))
    {
        return missing_argument_result(operation, "connect_key");
    }
    return CoreResult::success(operation);
}

CoreResult require_mount_point(const char *operation, const char *mount_point)
{
    if (missing_text(mount_point))
    {
        return missing_argument_result(operation, "mount_point");
    }
    return CoreResult::success(operation);
}

CoreResult require_callback(const char *operation, CasterCallback cb)
{
    if (!cb)
    {
        return missing_argument_result(operation, "callback");
    }
    return CoreResult::success(operation);
}
} // namespace

// redis_msg_internal *caster_svr = nullptr;

// int CASTER::Init(const char *json_conf, event_base *base)
// {
//     json conf = json::parse(json_conf);

//     if (caster_svr == nullptr)
//     {
//         caster_svr = new redis_msg_internal(conf, base);
//         caster_svr->start();
//     }
//     return 0;
// }

// int CASTER::Free()
// {
//     caster_svr->stop();
//     delete caster_svr;
//     return 0;
// }

// int CASTER::Clear()
// {
//     // 清除与指定在线表（单实例部署）
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "DEL MOUNT:ONLINE:COMMON");
//     return 0;
// }

// int CASTER::Clear(const char *server_key)
// {
//     // 清除与指定server_key(服务端口）相关的所有连接（暂未实现（用于集群部署）
//     return 0;
// }

// int CASTER::Check_Mount_Type(const char *mount_point)
// {
//     return CASTER::STATION_COMMON;
// }

// int CASTER::Set_Base_Station_State_ONLINE(const char *mount_point, const char *user_name, const char *connect_key, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "HSET MOUNT:ONLINE:COMMON %s %s", mount_point, connect_key); // 改成   挂载点-时间的格式
//     redisAsyncCommand(context, NULL, NULL, "HSET CHANNEL:ACTIVE MOUNT:%s %s", mount_point, connect_key);
//     caster_svr->add_local_active_connect_key(connect_key);

//     // 添加到本地的挂载点列表中（mount-connect_key)
//     // 向云端添加一条在线记录        HSET MOUNT:ONLINE:COMMON         挂载点：当前时间
//     // 向云端添加活跃频道记录        HSET CHANNEL:ACTIVE              挂载点：Connect_key
//     // 向云端添加订阅该频道的记录     HSET CHANNEL:SUBS:挂载点         Connect_Key:name

//     return 0;
// }

// int CASTER::Set_Base_Station_State_OFFLINE(const char *mount_point, const char *user_name, const char *connect_key, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "HDEL MOUNT:ONLINE:COMMON %s", mount_point);
//     redisAsyncCommand(context, NULL, NULL, "HDEL CHANNEL:ACTIVE MOUNT:%s", mount_point);
//     caster_svr->del_local_active_connect_key(connect_key);

//     return 0;
// }

// int CASTER::Check_Base_Station_is_ONLINE(const char *mount_point, CasterCallback cb, void *arg, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     auto ctx = new caster_cb_item();
//     ctx->cb = cb;
//     ctx->arg = arg;
//     redisAsyncCommand(context, redis_msg_internal::Redis_ONCE_Callback, ctx, "HEXISTS MOUNT:ONLINE:COMMON %s", mount_point);
//     return 0;
// }

// int CASTER::Pub_Base_Station_Raw_Data(const char *mount_point, const char *data, size_t data_length, const char *connect_key, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     redisAsyncCommand(context, NULL, NULL, "PUBLISH %s %b", channel.c_str(), data, data_length);
//     return 0;
// }

// int CASTER::Get_Base_Station_Sub_Num(const char *mount_point, CasterCallback cb, void *arg, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     auto ctx = new caster_cb_item();
//     ctx->cb = cb;
//     ctx->arg = arg;
//     redisAsyncCommand(context, redis_msg_internal::Redis_ONCE_Callback, ctx, "SCARD CHANNEL:%s:SUBS", channel.c_str());
//     return 0;
// }

// int CASTER::Sub_Base_Station_Raw_Data(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg, Station_type type)
// {
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     caster_svr->add_sub_cb_item(channel.c_str(), connect_key, cb, arg);
//     return 0;
// }

// int CASTER::UnSub_Base_Station_Raw_Data(const char *mount_point, const char *connect_key, Station_type type)
// {
//     std::string channel;
//     channel += "MOUNT:";
//     channel += mount_point;
//     caster_svr->del_sub_cb_item(channel.c_str(), connect_key);
//     return 0;
// }

// int CASTER::Set_Rover_Client_State_ONLINE(const char *mount_point, const char *user_name, const char *connect_key, Client_type type)
// {
//     // auto context = caster_svr->_pub_context;
//     // redisAsyncCommand(context, NULL, NULL, "HSET CLIENT:ONLINE:COMMON %s %s", mount_point, connect_key);
//     return 0;
// }

// int CASTER::Set_Rover_Client_State_OFFLINE(const char *mount_point, const char *user_name, const char *connect_key, Client_type type)
// {
//     return 0;
// }

// int CASTER::Check_Rover_Client_is_ONLINE(const char *user_name, CasterCallback cb, void *arg, Client_type type)
// {
//     return 0;
// }

// int CASTER::Pub_Rover_Client_Raw_Data(const char *client_key, const char *data, size_t data_length, const char *connect_key, Client_type type)
// {
//     auto context = caster_svr->_pub_context;
//     redisAsyncCommand(context, NULL, NULL, "PUBLISH CLIENT:%s %b", client_key, data, data_length);
//     return 0;
// }

// int CASTER::Sub_Rover_Client_Raw_Data(const char *client_key, CasterCallback cb, void *arg, const char *connect_key, Client_type type)
// {
//     std::string channel;
//     channel += "CLIENT:";
//     channel += client_key;
//     caster_svr->add_sub_cb_item(channel.c_str(), connect_key, cb, arg);
//     return 0;
// }

// int CASTER::Get_Rover_Client_Sub_Num(const char *mount_point, CasterCallback cb, void *arg, Station_type type)
// {
//     auto context = caster_svr->_pub_context;
//     std::string channel;
//     channel += "CLIENT:";
//     channel += mount_point;
//     auto ctx = new caster_cb_item();
//     ctx->cb = cb;
//     ctx->arg = arg;
//     redisAsyncCommand(context, redis_msg_internal::Redis_ONCE_Callback, ctx, "SCARD CHANNEL:%s:SUBS", channel.c_str());
//     return 0;
// }

// int CASTER::UnSub_Rover_Client_Raw_Data(const char *client_key, const char *connect_key, Client_type type)
// {
//     std::string channel;
//     channel += "CLIENT:";
//     channel += client_key;
//     caster_svr->del_sub_cb_item(channel.c_str(), connect_key);
//     return 0;
// }

// int CASTER::Get_Source_Table_List(CasterCallback cb, void *arg, Source_type type)
// {
//     return 0;
// }

// int CASTER::Add_Source_Table_Item(const char *mount_point, const char *info, double lon, double lat, Source_type type)
// {
//     return 0;
// }

// int CASTER::Del_Source_Table_Item(const char *mount_point, Source_type type)
// {
//     return 0;
// }

// int CASTER::Get_Source_Table_Item(const char *mount_point, CasterCallback cb, void *arg, Source_type type)
// {
//     return 0;
// }

// int CASTER::Get_Radius_Table_List(double lon, double lat, CasterCallback cb, void *arg, Source_type type)
// {
//     return 0;
// }

// std::string CASTER::Get_Source_Table_Text()
// {
//     return caster_svr->get_source_list_text();
// }

int CASTER::Init(CasterCoreOpt opt, event_base *base)
{
    caster_internal::getInstance()->init(opt, base);
    caster_internal::getInstance()->start();
    return 0;
}

int CASTER::Free()
{
    caster_internal::getInstance()->stop();
    return 0;
}

void CASTER::Set_Node_Runtime_Info(uint32_t listen_port, uint32_t http_port, uint32_t process_id)
{
    char host[256] = {0};
#ifdef _WIN32
    DWORD host_size = sizeof(host);
    if (GetComputerNameA(host, &host_size) == 0)
    {
        std::strncpy(host, "unknown", sizeof(host) - 1);
    }
#else
    if (gethostname(host, sizeof(host) - 1) != 0)
    {
        std::strncpy(host, "unknown", sizeof(host) - 1);
    }
#endif
    caster_internal::getInstance()->set_node_identity(host, static_cast<int>(listen_port), static_cast<int>(http_port));
    (void)process_id; // pid 通过 set_node_identity 内部 getpid() 获取
}

bool CASTER::Is_Master_Node()
{
    return caster_internal::getInstance()->is_master();
}

std::string CASTER::Get_Node_ID()
{
    return caster_internal::getInstance()->node_id();
}

std::string CASTER::Get_Status()
{
    return caster_internal::getInstance()->get_status_str();
}

bool CASTER::Check_Nearest_Mpt(const char *mount_point)
{
    if (missing_text(mount_point))
    {
        return false;
    }
    return caster_internal::getInstance()->is_nearest_mpt(mount_point);
}

bool CASTER::Check_Alias_Mpt(const char *mount_point)
{
    if (missing_text(mount_point))
    {
        return false;
    }
    return caster_internal::getInstance()->is_alias_mpt(mount_point);
}

int CASTER::Register_Record(const char *connect_key, const char *mount_point, const char *user_name, CasterCallback cb, void *arg, CasterRegisterType type, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);

    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return Register_Base_Record(mount_point, safe_optional_text(user_name), connect_key, cb, arg, type, safe_group_uid(group_uid));
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::ALIAS:
    case CasterRegisterType::PUSH:
        return Register_Rover_Record(mount_point, safe_optional_text(user_name), connect_key, cb, arg, type, safe_group_uid(group_uid));
    default:
        return finish_core_facade_failure(unsupported_type_result(__func__, type), cb, arg);
    }
}

int CASTER::Withdraw_Record(const char *connect_key, const char *mount_point, const char *user_name, CasterRegisterType type)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result);

    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return Withdraw_Base_Record(mount_point, safe_optional_text(user_name), connect_key);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::ALIAS:
    case CasterRegisterType::PUSH:
        return Withdraw_Rover_Record(mount_point, safe_optional_text(user_name), connect_key);
    default:
        return finish_core_facade_failure(unsupported_type_result(__func__, type));
    }
}

int CASTER::Pub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, const char *data, size_t data_length, CasterRegisterType type)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (data_length > 0 && data == nullptr)
        return finish_core_facade_failure(missing_argument_result(__func__, "data"));

    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        if (auto result = require_mount_point(__func__, mount_point); !result.ok())
            return finish_core_facade_failure(result);
        return Pub_Base_Raw_Data(mount_point, connect_key, data, data_length);
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::ALIAS:
    case CasterRegisterType::PUSH:
        return Pub_Rover_Raw_Data(safe_optional_text(user_name), connect_key, data, data_length);
    default:
        return finish_core_facade_failure(unsupported_type_result(__func__, type));
    }
}

int CASTER::Sub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, CasterCallback cb, void *arg, CasterRegisterType type, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);

    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        return Sub_Rover_Raw_Data(mount_point, safe_optional_text(user_name), connect_key, cb, arg, safe_group_uid(group_uid));
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::NEAREST:
    case CasterRegisterType::ALIAS:
    case CasterRegisterType::PUSH:
        return Sub_Base_Raw_Data(mount_point, safe_optional_text(user_name), connect_key, cb, arg, safe_group_uid(group_uid));
    default:
        return finish_core_facade_failure(unsupported_type_result(__func__, type), cb, arg);
    }
}

int CASTER::Unsub_Raw_Data(const char *connect_key, const char *mount_point, const char *user_name, CasterRegisterType type)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);

    switch (type)
    {
    case CasterRegisterType::SERVER:
    case CasterRegisterType::PULL:
        Unsub_Rover_Raw_Data(safe_optional_text(user_name), connect_key);
        return 0;
    case CasterRegisterType::CLIENT:
    case CasterRegisterType::ALIAS:
    case CasterRegisterType::PUSH:
        if (auto result = require_mount_point(__func__, mount_point); !result.ok())
            return finish_core_facade_failure(result);
        Unsub_Base_Raw_Data(mount_point, connect_key);
        return 0;
    case CasterRegisterType::NEAREST:
        Unsub_Near_Raw_Data(connect_key);
        return 0;
    default:
        return finish_core_facade_failure(unsupported_type_result(__func__, type));
    }
}

int CASTER::Register_Base_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->register_base_channel(mount_point, safe_optional_text(user_name), connect_key, cb, arg, type, safe_group_uid(group_uid));
}

int CASTER::Withdraw_Base_Record(const char *mount_point, const char *user_name, const char *connect_key)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->withdraw_base_channel(mount_point, safe_optional_text(user_name), connect_key);
}

int CASTER::Pub_Base_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result);
    if (data_length > 0 && data == nullptr)
        return finish_core_facade_failure(missing_argument_result(__func__, "data"));
    return caster_internal::getInstance()->pub_base_channel(mount_point, connect_key, data, data_length);
}

int CASTER::Sub_Base_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->sub_base_channel(mount_point, safe_optional_text(user_name), connect_key, cb, arg, safe_group_uid(group_uid));
}

int CASTER::Sub_Near_Raw_Data(const char *mount_point, double lat, double lon, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->sub_near_channel(mount_point, safe_optional_text(user_name), lat, lon, connect_key, cb, arg, safe_group_uid(group_uid));
}

int CASTER::Unsub_Near_Raw_Data(const char *connect_key)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->unsub_near_channel(connect_key);
}

int CASTER::Sub_Alias_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->sub_alias_channel(mount_point, safe_optional_text(user_name), connect_key, cb, arg, safe_group_uid(group_uid));
}

int CASTER::Unsub_Base_Raw_Data(const char *mount_point, const char *connect_key)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->unsub_base_channel(mount_point, connect_key);
}

int CASTER::Register_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->register_rover_channel(mount_point, safe_optional_text(user_name), connect_key, cb, arg, type, safe_group_uid(group_uid));
}

int CASTER::Withdraw_Rover_Record(const char *mount_point, const char *user_name, const char *connect_key)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->withdraw_rover_channel(mount_point, safe_optional_text(user_name), connect_key);
}

int CASTER::Pub_Rover_Raw_Data(const char *user_name, const char *connect_key, const char *data, size_t data_length)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    if (data_length > 0 && data == nullptr)
        return finish_core_facade_failure(missing_argument_result(__func__, "data"));
    return caster_internal::getInstance()->pub_rover_channel(safe_optional_text(user_name), connect_key, data, data_length);
}

int CASTER::Sub_Rover_Raw_Data(const char *mount_point, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, const char *group_uid)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_mount_point(__func__, mount_point); !result.ok())
        return finish_core_facade_failure(result, cb, arg);
    if (auto result = require_callback(__func__, cb); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->sub_rover_channel(mount_point, safe_optional_text(user_name), connect_key, cb, arg, safe_group_uid(group_uid));
}

int CASTER::Unsub_Rover_Raw_Data(const char *user_name, const char *connect_key)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->unsub_rover_channel(safe_optional_text(user_name), connect_key);
}

int CASTER::Set_Base_Source_Info(const char *mount_point, const char *connect_key, mount_info)
{
    // 自动上报的消息，更新的mount_point的挂载点显示信息
    // 同时写入到GEO表中 MPT:INFO:GEO

    // MPT:

    return 0;
}

int CASTER::Set_Base_Source_Info(const char *mount_point, const char *connect_key, const std::string &format_details, const std::string &nav_system)
{
    return caster_internal::getInstance()->set_base_source_info(mount_point, connect_key, format_details, nav_system);
}

int CASTER::Register_Grid_Record(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Withdraw_Grid_Record(const char *mount_point, const char *connect_key)
{
    return 0;
}

int CASTER::Pub_Grid_Raw_Data(const char *mount_point, const char *connect_key, const char *data, size_t data_length)
{
    return 0;
}

int CASTER::Sub_Grid_Raw_Data(const char *mount_point, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Sub_Grid_Raw_Data(double lat, double lon, const char *connect_key, CasterCallback cb, void *arg)
{
    return 0;
}

int CASTER::Unsub_Grid_Raw_Data(const char *mount_point, const char *connect_key)
{
    return 0;
}

int CASTER::Set_Grid_Source_Info(const char *mount_point, const char *connect_key, mount_info)
{
    return 0;
}

std::string CASTER::Get_Source_Table_Text(const char *group_uid)
{
    return caster_internal::getInstance()->get_source_list_text(group_uid == nullptr ? "default" : group_uid);
}

int CASTER::Relay_Register_Callback(RelayCallback cb, void *arg)
{
    return caster_internal::getInstance()->relay_register_callback(cb, arg);
}

int CASTER::Set_Base_Coord_Info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z)
{
    return caster_internal::getInstance()->set_base_coord_info(mount_point, connect_key, ecef_x, ecef_y, ecef_z);
}

int CASTER::Set_Pull_Base_Info(const char *task_key, const char *alias_mpt, const char *connect_key, int state)
{
    return caster_internal::getInstance()->update_pull_base_info(task_key, alias_mpt, connect_key, state);
}

int CASTER::Set_Push_Rover_Info(const char *task_key, const char *alias_mpt, const char *connect_key, int state)
{
    return caster_internal::getInstance()->update_push_rover_info(task_key, alias_mpt, connect_key, state);
}

int CASTER::Set_Rover_Coord_Info(const char *user_name, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, int Q, int sat, double diff)
{
    if (auto result = require_connect_key(__func__, connect_key); !result.ok())
        return finish_core_facade_failure(result);
    return caster_internal::getInstance()->set_rover_coord_info(safe_optional_text(user_name), connect_key, ecef_x, ecef_y, ecef_z, Q, sat, diff);
}


int CASTER::Set_Delay_Info(const char *connect_key, uint64_t delay)
{
    return caster_internal::getInstance()->set_connect_delay_info(connect_key, delay);
}
