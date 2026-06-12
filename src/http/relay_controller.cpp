#include "relay_controller.h"

#include "controller_helpers.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
} // namespace

RelayController::RelayController(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse RelayController::list_records(storage::RelayKind kind)
{
    storage::RelayRepository repo(_redis);
    return json_response(200, repo.list_records(kind));
}

ControllerResponse RelayController::get_record(storage::RelayKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }

    storage::RelayRepository repo(_redis);
    json data = repo.get_record(kind, uid);
    if (data.is_null())
    {
        return error_response(404, "Not found");
    }
    return json_response(200, data);
}

ControllerResponse RelayController::create_record(storage::RelayKind kind, const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    storage::RelayRepository repo(_redis);
    auto result = repo.create_record(kind, std::move(body));
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(201, json{{"ok", true}, {"uid", result.uid}});
}

ControllerResponse RelayController::update_record(storage::RelayKind kind, const std::string &uid, const std::string &body_text)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }

    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    storage::RelayRepository repo(_redis);
    auto result = repo.update_record(kind, uid, std::move(body));
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse RelayController::delete_record(storage::RelayKind kind, const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }

    storage::RelayRepository repo(_redis);
    auto result = repo.delete_record(kind, uid);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse RelayController::list_states(storage::RelayKind kind)
{
    storage::RelayRepository repo(_redis);
    return json_response(200, repo.list_states(kind));
}

ControllerResponse RelayController::set_enabled(storage::RelayKind kind, const std::string &uid, bool enabled)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }

    storage::RelayRepository repo(_redis);
    auto result = repo.set_enabled(kind, uid, enabled);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}, {"uid", uid}});
}

} // namespace navcaster::http_api
