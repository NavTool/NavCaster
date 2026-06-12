#include "relay_scheduler.h"

namespace navcaster::core
{
namespace
{
RelayScheduleAction make_action(caster::core::BroadcastType type,
                                caster::core::BroadcastOperateType operate,
                                const std::string &target,
                                const std::string &message,
                                const std::string &reason,
                                RelayDistributedMutation mutation = RelayDistributedMutation::None,
                                const std::string &mutation_key = std::string(),
                                const std::string &mutation_value = std::string())
{
    RelayScheduleAction action;
    action.message.type = type;
    action.message.operate = operate;
    action.message.target = target;
    action.message.msg_str = message;
    action.message.reason_str = reason;
    action.distributed_mutation = mutation;
    action.distributed_key = mutation_key;
    action.distributed_value = mutation_value;
    return action;
}

template <typename RecordMap, typename StatusMap>
std::vector<RelayScheduleAction> plan_distribution(const RecordMap &records,
                                                   StatusMap &statuses,
                                                   const RelayDistributedMap &distributed,
                                                   caster::core::BroadcastType type,
                                                   const char *active_reason,
                                                   const char *changed_reason,
                                                   const char *inactive_reason)
{
    std::vector<RelayScheduleAction> actions;

    for (const auto &record : records)
    {
        auto status = statuses.find(record.first);
        std::string current_json = record.second.toString();
        if (status == statuses.end() && record.second.is_enabled())
        {
            actions.push_back(make_action(type,
                                          caster::core::BOARDCAST_OPERATR_ACTIVE,
                                          record.first,
                                          current_json,
                                          active_reason,
                                          RelayDistributedMutation::StoreRecord,
                                          record.first,
                                          current_json));
        }
        else if (status != statuses.end() && record.second.is_enabled())
        {
            auto distributed_item = distributed.find(record.first);
            if (distributed_item == distributed.end() || distributed_item->second != current_json)
            {
                actions.push_back(make_action(type,
                                              caster::core::BOARDCAST_OPERATR_INACTIVE,
                                              record.first,
                                              status->second.toString(),
                                              changed_reason,
                                              RelayDistributedMutation::Erase,
                                              record.first));
            }
        }
    }

    for (auto &status : statuses)
    {
        auto record = records.find(status.first);
        if (record == records.end() || !record->second.is_enabled())
        {
            actions.push_back(make_action(type,
                                          caster::core::BOARDCAST_OPERATR_INACTIVE,
                                          status.first,
                                          status.second.toString(),
                                          inactive_reason,
                                          RelayDistributedMutation::Erase,
                                          status.first));
        }
    }

    return actions;
}
} // namespace

std::vector<RelayScheduleAction> RelayScheduler::plan_pull_distribution(const PullRecordMap &records,
                                                                        PullStatusMap &statuses,
                                                                        const RelayDistributedMap &distributed)
{
    return plan_distribution(records,
                             statuses,
                             distributed,
                             caster::core::BOARDCAST_TYPE_PULL_OPERATE,
                             "Pull Task Active",
                             "Pull Task Config Changed",
                             "Pull Task Inactive");
}

std::vector<RelayScheduleAction> RelayScheduler::plan_push_distribution(const PushRecordMap &records,
                                                                        PushStatusMap &statuses,
                                                                        const RelayDistributedMap &distributed)
{
    return plan_distribution(records,
                             statuses,
                             distributed,
                             caster::core::BOARDCAST_TYPE_RUSH_OPERATE,
                             "Push Task Active",
                             "Push Task Config Changed",
                             "Push Task Inactive");
}

void RelayScheduler::apply_distributed_mutation(const RelayScheduleAction &action,
                                                RelayDistributedMap &distributed)
{
    switch (action.distributed_mutation)
    {
    case RelayDistributedMutation::StoreRecord:
        distributed[action.distributed_key] = action.distributed_value;
        break;
    case RelayDistributedMutation::Erase:
        distributed.erase(action.distributed_key);
        break;
    case RelayDistributedMutation::None:
        break;
    }
}
} // namespace navcaster::core
