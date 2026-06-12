#include "config_controller.h"

#include "config_repository.h"
#include "controller_helpers.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
} // namespace

ConfigController::ConfigController(storage::RedisHashClient &redis, ConfigControllerDefaults defaults)
    : _redis(redis), _defaults(std::move(defaults))
{
}

bool ConfigController::save_config(const std::string &section, const std::string &json_text)
{
    storage::ConfigSection parsed_section;
    if (!storage::parse_config_section(section, parsed_section))
    {
        return false;
    }
    storage::ConfigRepository repo(_redis);
    return repo.save_config(parsed_section, json_text);
}

ControllerResponse ConfigController::get_configs()
{
    storage::ConfigRepository repo(_redis);
    return json_response(200, repo.list_configs());
}

ControllerResponse ConfigController::get_config(const std::string &section)
{
    storage::ConfigSection parsed_section;
    if (!storage::parse_config_section(section, parsed_section))
    {
        return error_response(404, "Unknown config section");
    }

    storage::ConfigRepository repo(_redis);
    json data = repo.get_config(parsed_section);
    if (parsed_section == storage::ConfigSection::Auth)
    {
        json result;
        if (data.is_object() && data.contains("admin_user"))
        {
            result["admin_user"] = data["admin_user"];
        }
        else
        {
            result["admin_user"] = _defaults.admin_user;
        }
        return json_response(200, result);
    }

    if (data.is_null())
    {
        return error_response(404, "Config not found");
    }
    return json_response(200, data);
}

ControllerResponse ConfigController::update_config(const std::string &section, const std::string &body_text)
{
    storage::ConfigSection parsed_section;
    if (!storage::parse_config_section(section, parsed_section))
    {
        return error_response(404, "Unknown config section");
    }

    json body;
    try
    {
        body = json::parse(body_text);
    }
    catch (...)
    {
        return error_response(400, "Invalid JSON");
    }

    storage::ConfigRepository repo(_redis);
    if (parsed_section == storage::ConfigSection::Auth)
    {
        const std::string old_password = body.value("old_password", "");
        if (old_password.empty())
        {
            return error_response(400, "需要输入当前密码");
        }

        std::string current_password = _defaults.admin_password;
        json auth_conf = repo.get_config(storage::ConfigSection::Auth);
        if (auth_conf.is_object() && auth_conf.contains("admin_password"))
        {
            current_password = auth_conf.value("admin_password", _defaults.admin_password);
        }

        if (old_password != current_password)
        {
            return error_response(403, "当前密码错误");
        }

        body.erase("old_password");
    }

    auto result = repo.update_config(parsed_section, body);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return error_response(500, "Redis error");
    }
    return json_response(200, json{{"ok", true}});
}

} // namespace navcaster::http_api
