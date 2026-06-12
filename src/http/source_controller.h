#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <cstdint>
#include <string>

namespace navcaster::http_api
{

class SourceController
{
public:
    SourceController(storage::RedisHashClient &redis, std::int64_t now);

    ControllerResponse list_sources();
    ControllerResponse get_source(const std::string &mountpoint);
    ControllerResponse create_source(const std::string &body_text);
    ControllerResponse update_source(const std::string &mountpoint, const std::string &body_text);
    ControllerResponse delete_source(const std::string &mountpoint);

private:
    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
