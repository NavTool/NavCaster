#pragma once

#include "broadcast_msg.h"
#include "pull_record.h"
#include "pull_status.h"
#include "push_record.h"
#include "push_status.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace navcaster::core
{
using PullRecordMap = std::unordered_map<std::string, pull_record>;
using PushRecordMap = std::unordered_map<std::string, push_record>;
using PullStatusMap = std::unordered_map<std::string, pull_status>;
using PushStatusMap = std::unordered_map<std::string, push_status>;
using RelayDistributedMap = std::unordered_map<std::string, std::string>;

enum class RelayDistributedMutation
{
    None,
    StoreRecord,
    Erase
};

struct RelayScheduleAction
{
    broadcast_msg message;
    RelayDistributedMutation distributed_mutation = RelayDistributedMutation::None;
    std::string distributed_key;
    std::string distributed_value;
};

class RelayScheduler
{
public:
    static std::vector<RelayScheduleAction> plan_pull_distribution(const PullRecordMap &records,
                                                                   PullStatusMap &statuses,
                                                                   const RelayDistributedMap &distributed);
    static std::vector<RelayScheduleAction> plan_push_distribution(const PushRecordMap &records,
                                                                   PushStatusMap &statuses,
                                                                   const RelayDistributedMap &distributed);
    static void apply_distributed_mutation(const RelayScheduleAction &action,
                                           RelayDistributedMap &distributed);
};
} // namespace navcaster::core
