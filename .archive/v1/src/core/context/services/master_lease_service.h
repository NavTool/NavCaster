#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::core
{
enum class MasterLeaseEventType
{
    None,
    Acquired,
    Lost
};

struct MasterLeaseObservedState
{
    std::string current_master_id;
    bool changed = false;
    bool is_self = false;
};

struct MasterLeaseEventPlan
{
    MasterLeaseEventType event = MasterLeaseEventType::None;
    bool is_master = false;
    bool trigger_cluster_sync = false;
    std::string log_key;
    std::string log_field;
    nlohmann::json payload;
};

class MasterLeaseService
{
public:
    static MasterLeaseObservedState observe_master(const std::string &previous_master_id,
                                                   const std::string &observed_master_id,
                                                   const std::string &node_id);

    static MasterLeaseEventPlan apply_keepalive_result(bool was_master,
                                                       bool renewed,
                                                       const std::string &node_id,
                                                       long long timestamp);
};
} // namespace navcaster::core
