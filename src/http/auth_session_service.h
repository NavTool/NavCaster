#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

struct AdminCredential
{
    std::string username;
    std::string password;
};

struct AuthSessionSubject
{
    std::string username;
    std::string account_id;
    std::string role;
    std::string status;
    bool compat_admin = false;
    bool authenticated = false;
};

class AuthSessionService
{
public:
    using TokenGenerator = std::function<std::string()>;

    AuthSessionService();
    explicit AuthSessionService(TokenGenerator generator);

    ControllerResponse login(const std::string &body_text,
                             const AdminCredential &default_admin,
                             const nlohmann::json &redis_auth_config,
                             storage::RedisHashClient *account_redis = nullptr);
    ControllerResponse logout(const std::string &authorization_header);

    bool validate_token(const std::string &token) const;
    std::string lookup_user(const std::string &token) const;
    AuthSessionSubject lookup_subject(const std::string &token) const;
    void invalidate_token(const std::string &token);

private:
    std::string create_session(AuthSessionSubject subject);

    TokenGenerator _token_generator;
    mutable std::mutex _mutex;
    std::unordered_map<std::string, AuthSessionSubject> _sessions;
};

std::string bearer_token_from_authorization(const std::string &authorization_header);
std::string random_hex_token();

} // namespace navcaster::http_api
