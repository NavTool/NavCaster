#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"

#include <string>

namespace navcaster::http_api
{

struct ConfigControllerDefaults
{
    std::string admin_user;
    std::string admin_password;
};

class ConfigController
{
public:
    ConfigController(storage::RedisHashClient &redis, ConfigControllerDefaults defaults);

    bool save_config(const std::string &section, const std::string &json_text);
    ControllerResponse get_configs();
    ControllerResponse get_config(const std::string &section);
    ControllerResponse update_config(const std::string &section, const std::string &body_text);

private:
    storage::RedisHashClient &_redis;
    ConfigControllerDefaults _defaults;
};

} // namespace navcaster::http_api
