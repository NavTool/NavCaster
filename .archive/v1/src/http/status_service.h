#pragma once

#include "controller_response.h"

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

struct StatusSnapshot
{
    double cpu_percent = 0.0;
    std::size_t memory_bytes = 0;
    std::string caster_status;
    bool redis_caster_connected = false;
    bool redis_auth_connected = false;
    int ntrip_port = 0;
    nlohmann::json master_node = nullptr;
    unsigned long long sse_clients = 0;
    unsigned long long sse_max_clients = 0;
    std::string node_id;
    std::string log_level;
};

nlohmann::json build_status_body(const StatusSnapshot &snapshot);

class StatusService
{
public:
    ControllerResponse status(const StatusSnapshot &snapshot) const;
    ControllerResponse health() const;
};

} // namespace navcaster::http_api
