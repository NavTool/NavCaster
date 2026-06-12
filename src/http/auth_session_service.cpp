#include "auth_session_service.h"

#include "controller_helpers.h"

#include <random>

namespace navcaster::http_api
{
namespace
{
bool credentials_match(const std::string &user,
                       const std::string &password,
                       const AdminCredential &credential)
{
    return !credential.username.empty()
        && user == credential.username
        && password == credential.password;
}
} // namespace

std::string random_hex_token()
{
    static const char charset[] = "0123456789abcdef";
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string token;
    token.reserve(64);
    for (int i = 0; i < 64; ++i)
    {
        token += charset[dist(gen)];
    }
    return token;
}

std::string bearer_token_from_authorization(const std::string &authorization_header)
{
    if (authorization_header.size() > 7)
    {
        return authorization_header.substr(7);
    }
    return {};
}

AuthSessionService::AuthSessionService()
    : AuthSessionService(random_hex_token)
{
}

AuthSessionService::AuthSessionService(TokenGenerator generator)
    : _token_generator(std::move(generator))
{
}

ControllerResponse AuthSessionService::login(const std::string &body_text,
                                             const AdminCredential &default_admin,
                                             const nlohmann::json &redis_auth_config)
{
    nlohmann::json body;
    if (!parse_json_body(body_text, body))
    {
        return error_response(400, "Invalid JSON body");
    }

    const std::string user = body.value("username", "");
    const std::string password = body.value("password", "");
    const AdminCredential redis_admin{
        redis_auth_config.is_object() ? redis_auth_config.value("admin_user", "") : "",
        redis_auth_config.is_object() ? redis_auth_config.value("admin_password", "") : ""};

    if (credentials_match(user, password, default_admin) || credentials_match(user, password, redis_admin))
    {
        const std::string token = create_session(user);
        return json_response(200, {{"token", token}, {"username", user}});
    }

    return error_response(401, "Invalid credentials");
}

ControllerResponse AuthSessionService::logout(const std::string &authorization_header)
{
    const std::string token = bearer_token_from_authorization(authorization_header);
    if (!token.empty())
    {
        invalidate_token(token);
    }
    return json_response(200, {{"ok", true}});
}

bool AuthSessionService::validate_token(const std::string &token) const
{
    if (token.empty())
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    return _sessions.count(token) > 0;
}

std::string AuthSessionService::lookup_user(const std::string &token) const
{
    if (token.empty())
    {
        return {};
    }
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _sessions.find(token);
    return it == _sessions.end() ? std::string{} : it->second;
}

void AuthSessionService::invalidate_token(const std::string &token)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions.erase(token);
}

std::string AuthSessionService::create_session(const std::string &user)
{
    const std::string token = _token_generator ? _token_generator() : random_hex_token();
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions[token] = user;
    return token;
}

} // namespace navcaster::http_api
