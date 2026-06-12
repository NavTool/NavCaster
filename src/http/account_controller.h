#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <cstdint>
#include <string>

namespace navcaster::http_api
{

class AccountController
{
public:
    AccountController(storage::RedisHashClient &redis, std::int64_t now);

    ControllerResponse list_accounts();
    ControllerResponse get_account(const std::string &account);
    ControllerResponse create_account(const std::string &body_text);
    ControllerResponse update_account(const std::string &account, const std::string &body_text);
    ControllerResponse delete_account(const std::string &account);
    ControllerResponse list_active_sessions();

private:
    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
