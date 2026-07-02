#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <cstdint>
#include <string>

namespace navcaster::http_api
{

class AliasController
{
public:
    AliasController(storage::RedisHashClient &redis, std::int64_t now);

    ControllerResponse list_aliases();
    ControllerResponse get_alias(const std::string &uid);
    ControllerResponse create_alias(const std::string &body_text);
    ControllerResponse update_alias(const std::string &uid, const std::string &body_text);
    ControllerResponse delete_alias(const std::string &uid);

private:
    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
