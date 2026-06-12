#include "runtime_command_service.h"

#include "broadcast_msg.h"
#include "controller_helpers.h"
#include "redis_keys.h"

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

bool is_kickable_kind(storage::RuntimeStateKind kind)
{
    return kind == storage::RuntimeStateKind::Server || kind == storage::RuntimeStateKind::Client;
}

const char *not_found_message(storage::RuntimeStateKind kind)
{
    return kind == storage::RuntimeStateKind::Server ? "Server not found" : "Client not found";
}
} // namespace

RuntimeCommandService::RuntimeCommandService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse RuntimeCommandService::kick(storage::RuntimeStateKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing UID");
    }
    if (!is_kickable_kind(kind))
    {
        return error_response(400, "Unsupported runtime command target");
    }

    storage::RuntimeStateRepository repo(_redis);
    if (repo.get(kind, uid).is_null())
    {
        return error_response(404, not_found_message(kind));
    }

    const std::string reason = "Force offline by administrator";
    if (!_redis.publish(redis_keys::CASTER_BROADCAST, make_kick_broadcast_message(kind, uid, reason)))
    {
        return error_response(500, "Failed to publish kick broadcast");
    }

    return json_response(200, json{{"ok", true}, {"uid", uid}});
}

std::string make_kick_broadcast_message(storage::RuntimeStateKind kind, const std::string &uid, const std::string &reason)
{
    broadcast_msg item;
    item.type = kind == storage::RuntimeStateKind::Server
                    ? caster::core::BOARDCAST_TYPE_SERVER_OPERATE
                    : caster::core::BOARDCAST_TYPE_CLIENT_OPERATE;
    item.operate = caster::core::BOARDCAST_OPERATE_DELETE;
    item.target = uid;
    item.msg_str = "";
    item.reason_str = reason;
    return item.toString();
}

} // namespace navcaster::http_api
