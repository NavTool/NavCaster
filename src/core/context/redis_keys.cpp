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
std::string log_mpt(const std::string &mount) { return append(LOG_MPT_PREFIX, mount); }
std::string log_usr(const std::string &user) { return append(LOG_USR_PREFIX, user); }
std::string log_node(const std::string &node_id) { return append(LOG_NODE_PREFIX, node_id); }
std::string node_history(const std::string &node_id) { return append(NODE_HISTORY_PREFIX, node_id); }
std::string node_history_1m(const std::string &node_id) { return node_history(node_id) + ":1M"; }
std::string node_history_5m(const std::string &node_id) { return node_history(node_id) + ":5M"; }
std::string mpt_channel(const std::string &mount) { return std::string("MPT:") + mount; }
std::string usr_channel(const std::string &user) { return std::string("USR:") + user; }
std::string node_channel(const std::string &node_id) { return std::string("NODE:") + node_id; }

} // namespace navcaster::redis_keys
