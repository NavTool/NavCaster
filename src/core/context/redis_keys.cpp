#include "redis_keys.h"

namespace navcaster::redis_keys
{
namespace
{
std::string append(const char *prefix, const std::string &value)
{
    return std::string(prefix) + value;
}
} // namespace

std::string mpt_rec(const std::string &mount) { return append(MPT_REC_PREFIX, mount); }
std::string usr_rec(const std::string &user) { return append(USR_REC_PREFIX, user); }
std::string mpt_sub(const std::string &mount) { return append(MPT_SUB_PREFIX, mount); }
std::string usr_sub(const std::string &user) { return append(USR_SUB_PREFIX, user); }
std::string access_item(const std::string &group_uid) { return append(ACCESS_ITEM_PREFIX, group_uid); }
std::string act_rec(const std::string &account) { return append(ACT_REC_PREFIX, account); }
std::string act_und(const std::string &name) { return append(ACT_UND_PREFIX, name); }
std::string act_session(const std::string &account) { return append(ACT_SESSION_PREFIX, account); }
std::string acc_group(const std::string &account_id) { return append(ACC_GROUP_PREFIX, account_id); }
std::string acc_balance_ledger(const std::string &period) { return append(ACC_BALANCE_LEDGER_PREFIX, period); }
std::string aacc_owner(const std::string &account_id) { return append(AACC_OWNER_PREFIX, account_id); }
std::string mpgrp_member(const std::string &group_id) { return append(MPGRP_MEMBER_PREFIX, group_id); }
std::string sub_account(const std::string &account_id) { return append(SUB_ACCOUNT_PREFIX, account_id); }
std::string redeem_account(const std::string &account_id) { return append(REDEEM_ACCOUNT_PREFIX, account_id); }
std::string online_session(const std::string &account_id) { return append(ONLINE_SESSION_PREFIX, account_id); }
std::string bill_entry(const std::string &period) { return append(BILL_ENTRY_PREFIX, period); }
std::string bill_account(const std::string &account_id, const std::string &period) { return append(BILL_ACCOUNT_PREFIX, account_id) + ":" + period; }
std::string data_push(const std::string &period) { return append(DATA_PUSH_PREFIX, period); }
std::string supply_usage(const std::string &period) { return append(SUPPLY_USAGE_PREFIX, period); }
std::string supply_account(const std::string &account_id, const std::string &period) { return append(SUPPLY_ACCOUNT_PREFIX, account_id) + ":" + period; }
std::string supply_earning(const std::string &account_id, const std::string &period) { return append(SUPPLY_EARNING_PREFIX, account_id) + ":" + period; }
std::string station_event(const std::string &mountpoint) { return append(STATION_EVENT_PREFIX, mountpoint); }
std::string log_mpt(const std::string &mount) { return append(LOG_MPT_PREFIX, mount); }
std::string log_usr(const std::string &user) { return append(LOG_USR_PREFIX, user); }
std::string log_node(const std::string &node_id) { return append(LOG_NODE_PREFIX, node_id); }
std::string node_history(const std::string &node_id) { return append(NODE_HISTORY_PREFIX, node_id); }
std::string node_history_1m(const std::string &node_id) { return node_history(node_id) + ":1M"; }
std::string node_history_5m(const std::string &node_id) { return node_history(node_id) + ":5M"; }
std::string stat_daily(const std::string &date) { return append(STAT_DAILY_PREFIX, date); }
std::string mpt_channel(const std::string &mount) { return std::string("MPT:") + mount; }
std::string usr_channel(const std::string &user) { return std::string("USR:") + user; }
std::string node_channel(const std::string &node_id) { return std::string("NODE:") + node_id; }

} // namespace navcaster::redis_keys
