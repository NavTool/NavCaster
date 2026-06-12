#pragma once

#include "redis_hash_client.h"
#include "repository_status.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

enum class ConfigSection
{
    Service,
    Core,
    Auth
};

struct ConfigRepositoryResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    ConfigSection section = ConfigSection::Service;
    std::string error;
    nlohmann::json value;
};

const char *config_section_key(ConfigSection section);
bool parse_config_section(const std::string &section, ConfigSection &out);
std::string config_section_name(ConfigSection section);

class ConfigRepository
{
public:
    explicit ConfigRepository(RedisHashClient &redis);

    nlohmann::json list_configs();
    nlohmann::json get_config(ConfigSection section);
    ConfigRepositoryResult update_config(ConfigSection section, const nlohmann::json &value);
    bool save_config(ConfigSection section, const std::string &json_text);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
