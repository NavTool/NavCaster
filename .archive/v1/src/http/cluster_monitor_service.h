#pragma once

#include "cluster_monitor_repository.h"
#include "controller_response.h"
#include "redis_hash_client.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

nlohmann::json build_cluster_monitor_snapshot(const std::string &master_node,
                                              const nlohmann::json &nodes_raw,
                                              const nlohmann::json &pull_states,
                                              const nlohmann::json &push_states,
                                              long long now_ts,
                                              double redis_latency_ms);

class ClusterMonitorService
{
public:
    explicit ClusterMonitorService(storage::RedisHashClient &redis);

    ControllerResponse snapshot(long long now_ts);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
