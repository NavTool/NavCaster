#include "source_controller.h"

#include "controller_helpers.h"
#include "source_repository.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
} // namespace

SourceController::SourceController(storage::RedisHashClient &redis, std::int64_t now)
    : _redis(redis), _now(now)
{
}

ControllerResponse SourceController::list_sources()
{
    storage::SourceRepository repo(_redis);
    return json_response(200, repo.list_sources());
}

ControllerResponse SourceController::get_source(const std::string &mountpoint)
{
    if (mountpoint.empty())
    {
        return error_response(400, "Missing ID");
    }
    storage::SourceRepository repo(_redis);
    json data = repo.get_source(mountpoint);
    if (data.is_null())
    {
        return error_response(404, "Not found");
    }
    return json_response(200, data);
}

ControllerResponse SourceController::create_source(const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }
    storage::SourceRepository repo(_redis);
    auto result = repo.create_source(std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(201, json{{"ok", true}, {"mountpoint", result.mountpoint}});
}

ControllerResponse SourceController::update_source(const std::string &mountpoint, const std::string &body_text)
{
    if (mountpoint.empty())
    {
        return error_response(400, "Missing ID");
    }
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }
    storage::SourceRepository repo(_redis);
    auto result = repo.update_source(mountpoint, std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse SourceController::delete_source(const std::string &mountpoint)
{
    if (mountpoint.empty())
    {
        return error_response(400, "Missing ID");
    }
    storage::SourceRepository repo(_redis);
    auto result = repo.delete_source(mountpoint);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

} // namespace navcaster::http_api
