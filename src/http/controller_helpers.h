#pragma once

#include "controller_response.h"
#include "repository_status.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

inline ControllerResponse json_response(int status_code, const nlohmann::json &body)
{
    ControllerResponse response;
    response.status_code = status_code;
    response.body = body.dump();
    return response;
}

inline ControllerResponse error_response(int status_code, const std::string &message)
{
    return json_response(status_code, nlohmann::json{{"error", message}});
}

inline ControllerResponse repository_error(storage::RepositoryStatus status, const std::string &message)
{
    const std::string error = message.empty() ? "Repository error" : message;
    switch (status)
    {
    case storage::RepositoryStatus::Invalid:
        return error_response(400, error);
    case storage::RepositoryStatus::NotFound:
        return error_response(404, error);
    case storage::RepositoryStatus::Conflict:
        return error_response(409, error);
    case storage::RepositoryStatus::RedisError:
        return error_response(500, error);
    case storage::RepositoryStatus::Ok:
        break;
    }
    return error_response(500, error);
}

inline bool parse_json_body(const std::string &body_text, nlohmann::json &body)
{
    try
    {
        body = nlohmann::json::parse(body_text);
        return true;
    }
    catch (...)
    {
        body = nullptr;
        return false;
    }
}

} // namespace navcaster::http_api
