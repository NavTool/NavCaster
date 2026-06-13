#pragma once

#include "Auth_Verify.h"
#include "redis_keys.h"

#include <ctime>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::auth
{
inline std::string active_session_key(const std::string &account)
{
    return std::string(redis_keys::ACT_SESSION_PREFIX) + account;
}

inline const char *auth_type_name(AuthType type)
{
    switch (type)
    {
    case AuthType::SERVER:
        return "server";
    case AuthType::CLIENT:
        return "client";
    case AuthType::SOURCE:
        return "source";
    default:
        return "unknown";
    }
}

inline std::string build_active_session_record_json(
    const std::string &account,
    const std::string &connect_key,
    AuthType type,
    std::time_t online_time,
    std::time_t update_time,
    const std::string &group_uid)
{
    nlohmann::json record;
    record["uid"] = connect_key;
    record["connect_key"] = connect_key;
    record["account"] = account;
    record["anonymous"] = false;
    record["auth_type"] = auth_type_name(type);
    record["online_time"] = online_time;
    record["update_time"] = update_time;
    record["addr"] = "";
    record["port"] = "";
    record["group_uid"] = group_uid.empty() ? "default" : group_uid;
    return record.dump();
}
}
