#include "access_controller.h"

#include "access_repository.h"
#include "controller_helpers.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

std::string string_field_or(const json &body, const char *field, const std::string &fallback = {})
{
    if (!body.is_object())
    {
        return fallback;
    }

    const auto it = body.find(field);
    if (it == body.end() || it->is_null())
    {
        return fallback;
    }
    if (it->is_string())
    {
        return it->get<std::string>();
    }
    return fallback;
}

std::string access_item_mountpoint(const json &body)
{
    std::string mount = string_field_or(body, "mount_point_name");
    if (mount.empty())
    {
        mount = string_field_or(body, "mountpoint");
    }
    if (mount.empty())
    {
        mount = string_field_or(body, "mount");
    }
    if (mount.empty())
    {
        mount = string_field_or(body, "uid");
    }
    return mount;
}
} // namespace

AccessController::AccessController(storage::RedisHashClient &redis, std::int64_t now)
    : _redis(redis), _now(now)
{
}

ControllerResponse AccessController::list_groups()
{
    storage::AccessRepository repo(_redis);
    return json_response(200, repo.list_groups());
}

ControllerResponse AccessController::get_group(const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }
    storage::AccessRepository repo(_redis);
    json data = repo.get_group(uid);
    if (data.is_null())
    {
        return error_response(404, "Not found");
    }
    return json_response(200, data);
}

ControllerResponse AccessController::create_group(const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    storage::AccessRepository repo(_redis);
    auto result = repo.create_group(std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(201, json{{"ok", true}, {"uid", result.uid}});
}

ControllerResponse AccessController::update_group(const std::string &uid, const std::string &body_text)
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

    storage::AccessRepository repo(_redis);
    auto result = repo.update_group(uid, std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse AccessController::delete_group(const std::string &uid)
{
    if (uid.empty())
    {
        return error_response(400, "Missing ID");
    }

    storage::AccessRepository repo(_redis);
    auto result = repo.delete_group(uid);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        if (result.error == "Built-in group cannot be deleted")
        {
            return error_response(403, result.error);
        }
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse AccessController::list_items(const std::string &group_uid)
{
    if (group_uid.empty())
    {
        return error_response(400, "Missing group_uid");
    }

    storage::AccessRepository repo(_redis);
    return json_response(200, repo.list_items(group_uid));
}

ControllerResponse AccessController::create_item(const std::string &group_uid, const std::string &body_text)
{
    if (group_uid.empty())
    {
        return error_response(400, "Missing group_uid");
    }

    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    storage::AccessRepository repo(_redis);
    auto result = repo.create_item(group_uid, std::move(body));
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(201, json{{"ok", true}});
}

ControllerResponse AccessController::update_item(const std::string &group_uid, const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    const std::string resolved_group_uid = string_field_or(body, "group_uid", group_uid);
    storage::AccessRepository repo(_redis);
    auto result = repo.update_item(resolved_group_uid, std::move(body));
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse AccessController::delete_item(const std::string &group_uid, const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON, need group_uid and mountpoint");
    }

    const std::string resolved_group_uid = string_field_or(body, "group_uid", group_uid);
    const std::string mount = access_item_mountpoint(body);
    if (resolved_group_uid.empty() || mount.empty())
    {
        return error_response(400, "Missing group_uid or mountpoint");
    }

    storage::AccessRepository repo(_redis);
    auto result = repo.delete_item(resolved_group_uid, mount);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

} // namespace navcaster::http_api
