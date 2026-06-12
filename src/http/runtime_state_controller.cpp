#include "runtime_state_controller.h"

#include "controller_helpers.h"

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
} // namespace

RuntimeStateController::RuntimeStateController(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse RuntimeStateController::list(storage::RuntimeStateKind kind)
{
    storage::RuntimeStateRepository repo(_redis);
    return json_response(200, repo.list(kind));
}

ControllerResponse RuntimeStateController::get(storage::RuntimeStateKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }

    storage::RuntimeStateRepository repo(_redis);
    json data = repo.get(kind, uid);
    if (data.is_null())
    {
        return error_response(404, "Not found");
    }
    return json_response(200, data);
}

} // namespace navcaster::http_api
