#include "account_controller.h"

#include "account_repository.h"
#include "controller_helpers.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;
} // namespace

AccountController::AccountController(storage::RedisHashClient &redis, std::int64_t now)
    : _redis(redis), _now(now)
{
}

ControllerResponse AccountController::list_accounts()
{
    storage::AccountRepository repo(_redis);
    return json_response(200, repo.list_accounts());
}

ControllerResponse AccountController::get_account(const std::string &account)
{
    if (account.empty() || account == "active")
    {
        return list_active_sessions();
    }

    storage::AccountRepository repo(_redis);
    json data = repo.get_account(account);
    if (data.is_null())
    {
        return error_response(404, "Account not found");
    }
    return json_response(200, data);
}

ControllerResponse AccountController::create_account(const std::string &body_text)
{
    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    storage::AccountRepository repo(_redis);
    auto result = repo.create_account(std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(201, json{{"ok", true}, {"account", result.account}});
}

ControllerResponse AccountController::update_account(const std::string &account, const std::string &body_text)
{
    if (account.empty())
    {
        return error_response(400, "Missing account name");
    }

    json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON");
    }

    storage::AccountRepository repo(_redis);
    auto result = repo.update_account(account, std::move(body), _now);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse AccountController::delete_account(const std::string &account)
{
    if (account.empty())
    {
        return error_response(400, "Missing account name");
    }

    storage::AccountRepository repo(_redis);
    auto result = repo.delete_account(account);
    if (result.status != storage::RepositoryStatus::Ok)
    {
        return repository_error(result.status, result.error);
    }
    return json_response(200, json{{"ok", true}});
}

ControllerResponse AccountController::list_active_sessions()
{
    storage::AccountRepository repo(_redis);
    return json_response(200, repo.list_active_sessions());
}

} // namespace navcaster::http_api
