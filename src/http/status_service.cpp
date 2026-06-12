#include "status_service.h"

#include "controller_helpers.h"

namespace navcaster::http_api
{

nlohmann::json build_status_body(const StatusSnapshot &snapshot)
{
    nlohmann::json status;
    status["cpu_percent"] = snapshot.cpu_percent;
    status["memory_bytes"] = snapshot.memory_bytes;
    status["memory_mb"] = snapshot.memory_bytes / 1024.0 / 1024.0;

    try
    {
        status["caster"] = nlohmann::json::parse(snapshot.caster_status);
    }
    catch (...)
    {
        status["caster"] = snapshot.caster_status;
    }

    status["redis_caster_connected"] = snapshot.redis_caster_connected;
    status["redis_auth_connected"] = snapshot.redis_auth_connected;
    status["ntrip_port"] = snapshot.ntrip_port;
    status["master_node"] = snapshot.master_node.is_string() ? snapshot.master_node : nullptr;
    status["sse_clients"] = snapshot.sse_clients;
    status["sse_max_clients"] = snapshot.sse_max_clients;
    status["node_id"] = snapshot.node_id;
    status["log_level"] = snapshot.log_level;
    return status;
}

ControllerResponse StatusService::status(const StatusSnapshot &snapshot) const
{
    return json_response(200, build_status_body(snapshot));
}

ControllerResponse StatusService::health() const
{
    return json_response(200, {{"status", "ok"}});
}

} // namespace navcaster::http_api
