#include "auth_session_service.h"

#include "account_domain.h"
#include "account_schema.h"
#include "controller_helpers.h"
#include "json_record.h"
#include "redis_keys.h"

#include <random>
#include <utility>

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

void erase_sensitive_fields(nlohmann::json &record)
{
    static constexpr const char *fields[] = {
        "password",
        "old_password",
        "password_hash",
        "password_algo",
        "password_salt",
        "password_iterations",
    };
    for (const auto *field : fields)
    {
        record.erase(field);
    }
}

nlohmann::json subject_json(const AuthSessionSubject &subject, const std::string &token = {})
{
    nlohmann::json body = {
        {"username", subject.username},
        {"account_id", subject.account_id},
        {"role", subject.role},
        {"status", subject.status},
        {"compat_admin", subject.compat_admin},
    };
    if (!token.empty())
    {
        body["token"] = token;
    }
    return body;
}

bool account_password_matches(const nlohmann::json &account, const std::string &password)
{
    navcaster::account_schema::AccountAuthView view;
    nlohmann::json auth_record = {
        {"account", account.value("username", std::string{})},
    };
    if (account.contains("password"))
    {
        auth_record["password"] = account["password"];
    }
    if (account.contains("password_hash"))
    {
        auth_record["password_hash"] = account["password_hash"];
    }
    if (account.contains("password_algo"))
    {
        auth_record["password_algo"] = account["password_algo"];
    }
    if (account.contains("password_salt"))
    {
        auth_record["password_salt"] = account["password_salt"];
    }
    if (account.contains("password_iterations"))
    {
        auth_record["password_iterations"] = account["password_iterations"];
    }
    if (!navcaster::account_schema::parse_auth_view(auth_record.dump(), view))
    {
        return false;
    }
    return navcaster::account_schema::password_matches(view, password);
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
                                             const nlohmann::json &redis_auth_config,
                                             storage::RedisHashClient *account_redis)
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

    if (account_redis)
    {
        const auto username_index = account_redis->hget(redis_keys::ACC_USERNAME, user.c_str());
        const std::string account_id = username_index.is_object() ? username_index.value("id", std::string{}) : std::string{};
        if (!account_id.empty())
        {
            auto account = account_redis->hget(redis_keys::ACC_RECORD, account_id.c_str());
            if (account.is_object() && navcaster::account_domain::is_active_status(account) &&
                account_password_matches(account, password))
            {
                AuthSessionSubject subject;
                subject.authenticated = true;
                subject.username = account.value("username", user);
                subject.account_id = account.value("account_id", account_id);
                subject.role = account.value("role", std::string{});
                subject.status = account.value("status", std::string{});
                subject.compat_admin = false;
                const std::string token = create_session(subject);
                erase_sensitive_fields(account);
                auto body = subject_json(subject, token);
                body["account"] = account;
                return json_response(200, body);
            }
        }
    }

    if (credentials_match(user, password, default_admin) || credentials_match(user, password, redis_admin))
    {
        AuthSessionSubject subject;
        subject.authenticated = true;
        subject.username = user;
        subject.role = "admin";
        subject.status = "active";
        subject.compat_admin = true;
        const std::string token = create_session(subject);
        return json_response(200, subject_json(subject, token));
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
    return it == _sessions.end() ? std::string{} : it->second.username;
}

AuthSessionSubject AuthSessionService::lookup_subject(const std::string &token) const
{
    if (token.empty())
    {
        return {};
    }
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _sessions.find(token);
    return it == _sessions.end() ? AuthSessionSubject{} : it->second;
}

void AuthSessionService::invalidate_token(const std::string &token)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _sessions.erase(token);
}

std::string AuthSessionService::create_session(AuthSessionSubject subject)
{
    const std::string token = _token_generator ? _token_generator() : random_hex_token();
    std::lock_guard<std::mutex> lock(_mutex);
    subject.authenticated = true;
    _sessions[token] = std::move(subject);
    return token;
}

} // namespace navcaster::http_api
