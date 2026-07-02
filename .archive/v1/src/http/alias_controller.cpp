#include "alias_controller.h"

#include "alias_repository.h"
#include "controller_helpers.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
} // namespace

AliasController::AliasController(storage::RedisHashClient &redis, std::int64_t now)
    : _redis(redis), _now(now)
{
}

ControllerResponse AliasController::list_aliases()
{
    storage::AliasRepository repo(_redis);
    return json_response(200, repo.list_aliases());
}

ControllerResponse AliasController::get_alias(const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }
    storage::AliasRepository repo(_redis);
    json data = repo.get_alias(uid);
    if (data.is_null())
    {
        return error_response(404, "Not found");
    }
    return json_response(200, data);
}

ControllerResponse AliasController::create_alias(const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }
    storage::AliasRepository repo(_redis);
    auto result = repo.create_alias(std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(201, json{{"ok", true}, {"alias", result.uid}});
}

ControllerResponse AliasController::update_alias(const std::string &uid, const std::string &body_text)
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
    storage::AliasRepository repo(_redis);
    auto result = repo.update_alias(uid, std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse AliasController::delete_alias(const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }
    storage::AliasRepository repo(_redis);
    auto result = repo.delete_alias(uid);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

} // namespace navcaster::http_api
