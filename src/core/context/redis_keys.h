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

// Account/billing operations domain (NC-051+)
inline constexpr const char *ACC_RECORD = "ACC:RECORD";
inline constexpr const char *ACC_USERNAME = "ACC:USERNAME";
inline constexpr const char *ACC_GROUP_PREFIX = "ACC:GROUP:";
inline constexpr const char *ACC_BALANCE_LEDGER_PREFIX = "ACC:BALANCE:LEDGER:";
inline constexpr const char *AACC_RECORD = "AACC:RECORD";
inline constexpr const char *AACC_USERNAME = "AACC:USERNAME";
inline constexpr const char *AACC_ACTIVE = "AACC:ACTIVE";
inline constexpr const char *AACC_OWNER_PREFIX = "AACC:OWNER:";
inline constexpr const char *MPGRP_RECORD = "MPGRP:RECORD";
inline constexpr const char *MPGRP_MEMBER_PREFIX = "MPGRP:MEMBER:";
inline constexpr const char *MOUNT_RECORD = "MOUNT:RECORD";
inline constexpr const char *SUB_PLAN = "SUB:PLAN";
inline constexpr const char *SUB_RECORD = "SUB:RECORD";
inline constexpr const char *SUB_ACCOUNT_PREFIX = "SUB:ACCOUNT:";
inline constexpr const char *REDEEM_CODE = "REDEEM:CODE";
inline constexpr const char *REDEEM_ACCOUNT_PREFIX = "REDEEM:ACCOUNT:";
inline constexpr const char *ONLINE_SESSION_PREFIX = "ONLINE:SESSION:";
inline constexpr const char *BILL_ENTRY_PREFIX = "BILL:ENTRY:";
inline constexpr const char *BILL_ACCOUNT_PREFIX = "BILL:ACCOUNT:";
inline constexpr const char *BILL_IDEMPOTENT = "BILL:IDEMPOTENT";
inline constexpr const char *DATA_INGRESS_CONFIG = "DATA:INGRESS:CONFIG";
inline constexpr const char *DATA_PUSH_PREFIX = "DATA:PUSH:";
inline constexpr const char *DATA_PUSH_CONFIG = "DATA:PUSH:CONFIG";
inline constexpr const char *DATA_PUSH_MAINTENANCE = "DATA:PUSH:MAINTENANCE";
inline constexpr const char *DATA_PUSH_JOB_PREFIX = "DATA:PUSH:JOB:";
inline constexpr const char *OPS_ALERT_POLICY = "OPS:ALERT:POLICY";
inline constexpr const char *SUPPLY_USAGE_PREFIX = "SUPPLY:USAGE:";
inline constexpr const char *SUPPLY_ACCOUNT_PREFIX = "SUPPLY:ACCOUNT:";
inline constexpr const char *SUPPLY_EARNING_PREFIX = "SUPPLY:EARNING:";
inline constexpr const char *STATION_RECORD = "STATION:RECORD";
inline constexpr const char *STATION_EVENT_PREFIX = "STATION:EVENT:";

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
std::string acc_group(const std::string &account_id);
std::string acc_balance_ledger(const std::string &period);
std::string aacc_owner(const std::string &account_id);
std::string mpgrp_member(const std::string &group_id);
std::string sub_account(const std::string &account_id);
std::string redeem_account(const std::string &account_id);
std::string online_session(const std::string &account_id);
std::string bill_entry(const std::string &period);
std::string bill_account(const std::string &account_id, const std::string &period);
std::string data_push(const std::string &period);
std::string data_push_job(const std::string &period);
std::string supply_usage(const std::string &period);
std::string supply_account(const std::string &account_id, const std::string &period);
std::string supply_earning(const std::string &account_id, const std::string &period);
std::string station_event(const std::string &mountpoint);
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
