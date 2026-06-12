#pragma once

#include <string>

namespace navcaster::redis_keys
{

// Runtime cluster state
inline constexpr const char *CASTER_MASTER = "CASTER:MASTER";
inline constexpr const char *CASTER_NODE = "CASTER:NODE";

// Runtime connection state
inline constexpr const char *MPT_LIST = "MPT:LIST";
inline constexpr const char *USR_LIST = "USR:LIST";
inline constexpr const char *MPT_STAT = "MPT:STAT";
inline constexpr const char *USR_STAT = "USR:STAT";
inline constexpr const char *STR_STAT = "STR:STAT";
inline constexpr const char *MPT_REC_PREFIX = "MPT:REC:";
inline constexpr const char *USR_REC_PREFIX = "USR:REC:";
inline constexpr const char *MPT_SUB_PREFIX = "MPT:SUB:";
inline constexpr const char *USR_SUB_PREFIX = "USR:SUB:";
inline constexpr const char *MPT_GEO = "MPT:GEO";
inline constexpr const char *USR_GEO = "USR:GEO";

// Persistent caster configuration
inline constexpr const char *MPT_RECORD = "MPT:RECORD";
inline constexpr const char *MPT_SOURCE = "MPT:SOURCE";
inline constexpr const char *ALIAS_RULE = "ALIAS:RULE";
inline constexpr const char *ACCESS_GROUP = "ACCESS:GROUP";
inline constexpr const char *ACCESS_ITEM_PREFIX = "ACCESS:ITEM:";
inline constexpr const char *PULL_RECORD = "PULL:RECORD";
inline constexpr const char *PULL_STAT = "PULL:STAT";
inline constexpr const char *PUSH_RECORD = "PUSH:RECORD";
inline constexpr const char *PUSH_STAT = "PUSH:STAT";
inline constexpr const char *CONF_SERVICE = "CONF:SERVICE";
inline constexpr const char *CONF_CORE = "CONF:CORE";
inline constexpr const char *CONF_AUTH = "CONF:AUTH";

// Auth
inline constexpr const char *ACT_RECORD = "ACT:RECORD";
inline constexpr const char *ACT_ACTIVE = "ACT:ACTIVE";
inline constexpr const char *ACT_REC_PREFIX = "ACT:REC:";
inline constexpr const char *ACT_UND_PREFIX = "ACT:UND:";
inline constexpr const char *ACT_UNNAMED = "ACT:UNNAMED";
inline constexpr const char *ACT_SESSION_PREFIX = "ACT:SESSION:";
inline constexpr const char *STR_ACTIVE_LEGACY = "STR:ACTIVE";

// History and monitoring
inline constexpr const char *LOG_MPT_PREFIX = "LOG:MPT:";
inline constexpr const char *LOG_USR_PREFIX = "LOG:USR:";
inline constexpr const char *LOG_NODE_PREFIX = "LOG:NODE:";
inline constexpr const char *LOG_AUDIT = "LOG:AUDIT";
inline constexpr const char *LOG_AUDIT_SEQ = "LOG:AUDIT:SEQ";
inline constexpr const char *NODE_HISTORY_PREFIX = "NODE:HISTORY:";
inline constexpr const char *MONITOR_REDIS_HISTORY = "MONITOR:REDIS:HISTORY";
inline constexpr const char *STAT_DAILY_PREFIX = "STAT:DAILY:";

// Pub/Sub channels
inline constexpr const char *CASTER_BROADCAST = "CASTER:BROADCAST";
inline constexpr const char *CASTER_CONF = "CASTER:CONF";
inline constexpr const char *AUTH_BROADCAST = "AUTH:BROADCAST";

std::string mpt_rec(const std::string &mount);
std::string usr_rec(const std::string &user);
std::string mpt_sub(const std::string &mount);
std::string usr_sub(const std::string &user);
std::string access_item(const std::string &group_uid);
std::string act_rec(const std::string &account);
std::string act_und(const std::string &name);
std::string act_session(const std::string &account);
std::string log_mpt(const std::string &mount);
std::string log_usr(const std::string &user);
std::string log_node(const std::string &node_id);
std::string node_history(const std::string &node_id);
std::string node_history_1m(const std::string &node_id);
std::string node_history_5m(const std::string &node_id);
std::string stat_daily(const std::string &date);
std::string mpt_channel(const std::string &mount);
std::string usr_channel(const std::string &user);
std::string node_channel(const std::string &node_id);

} // namespace navcaster::redis_keys
