#include "master_lease_service.h"

#include "redis_keys.h"

namespace navcaster::core
{
namespace
{
constexpr const char *MASTER_ACQUIRED = "master_acquired";
constexpr const char *MASTER_LOST = "master_lost";

MasterLeaseEventPlan make_event(MasterLeaseEventType event,
                                bool is_master,
                                bool trigger_cluster_sync,
                                const std::string &node_id,
                                long long timestamp,
                                const char *event_name)
{
    MasterLeaseEventPlan plan;
    plan.event = event;
    plan.is_master = is_master;
    plan.trigger_cluster_sync = trigger_cluster_sync;
    plan.log_key = redis_keys::log_node(node_id);
    plan.log_field = std::to_string(timestamp) + "_" + event_name;
    plan.payload = {
        {"event", event_name},
        {"node_id", node_id},
        {"timestamp", timestamp},
    };
    return plan;
}
} // namespace

MasterLeaseObservedState MasterLeaseService::observe_master(const std::string &previous_master_id,
                                                            const std::string &observed_master_id,
                                                            const std::string &node_id)
{
    MasterLeaseObservedState state;
    state.current_master_id = observed_master_id;
    state.changed = previous_master_id != observed_master_id;
    state.is_self = observed_master_id == node_id;
    return state;
}

MasterLeaseEventPlan MasterLeaseService::apply_keepalive_result(bool was_master,
                                                                bool renewed,
                                                                const std::string &node_id,
                                                                long long timestamp)
{
    if (renewed && !was_master)
    {
        return make_event(MasterLeaseEventType::Acquired, true, true, node_id, timestamp, MASTER_ACQUIRED);
    }

    if (!renewed && was_master)
    {
        return make_event(MasterLeaseEventType::Lost, false, false, node_id, timestamp, MASTER_LOST);
    }

    MasterLeaseEventPlan plan;
    plan.is_master = was_master;
    plan.trigger_cluster_sync = renewed;
    return plan;
}
} // namespace navcaster::core
