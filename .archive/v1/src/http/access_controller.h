#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <cstdint>
#include <string>

namespace navcaster::http_api
{

class AccessController
{
public:
    AccessController(storage::RedisHashClient &redis, std::int64_t now);

    ControllerResponse list_groups();
    ControllerResponse get_group(const std::string &uid);
    ControllerResponse create_group(const std::string &body_text);
    ControllerResponse update_group(const std::string &uid, const std::string &body_text);
    ControllerResponse delete_group(const std::string &uid);

    ControllerResponse list_items(const std::string &group_uid);
    ControllerResponse create_item(const std::string &group_uid, const std::string &body_text);
    ControllerResponse update_item(const std::string &group_uid, const std::string &body_text);
    ControllerResponse delete_item(const std::string &group_uid, const std::string &body_text);

private:
    storage::RedisHashClient &_redis;
    std::int64_t _now = 0;
};

} // namespace navcaster::http_api
