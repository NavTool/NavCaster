#include "config_repository.h"

#include "json_record.h"
#include "redis_keys.h"

namespace navcaster::storage
{

const char *config_section_key(ConfigSection section)
{
    switch (section)
    {
    case ConfigSection::Service:
        return redis_keys::CONF_SERVICE;
    case ConfigSection::Core:
        return redis_keys::CONF_CORE;
    case ConfigSection::Auth:
        return redis_keys::CONF_AUTH;
    }
    return redis_keys::CONF_SERVICE;
}

bool parse_config_section(const std::string &section, ConfigSection &out)
{
    if (section == "service")
    {
        out = ConfigSection::Service;
        return true;
    }
    if (section == "core")
    {
        out = ConfigSection::Core;
        return true;
    }
    if (section == "auth")
    {
        out = ConfigSection::Auth;
        return true;
    }
    return false;
}

std::string config_section_name(ConfigSection section)
{
    switch (section)
    {
    case ConfigSection::Service:
        return "service";
    case ConfigSection::Core:
        return "core";
    case ConfigSection::Auth:
        return "auth";
    }
    return "service";
}

ConfigRepository::ConfigRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json ConfigRepository::list_configs()
{
    nlohmann::json result = nlohmann::json::object();
    const auto service = get_config(ConfigSection::Service);
    const auto core = get_config(ConfigSection::Core);
    const auto auth = get_config(ConfigSection::Auth);
    if (!service.is_null())
    {
        result[config_section_name(ConfigSection::Service)] = service;
    }
    if (!core.is_null())
    {
        result[config_section_name(ConfigSection::Core)] = core;
    }
    if (!auth.is_null())
    {
        result[config_section_name(ConfigSection::Auth)] = auth;
    }
    return result;
}

nlohmann::json ConfigRepository::get_config(ConfigSection section)
{
    return _redis.get(config_section_key(section));
}

ConfigRepositoryResult ConfigRepository::update_config(ConfigSection section, const nlohmann::json &value)
{
    ConfigRepositoryResult result;
    result.section = section;
    result.value = value;
    if (!_redis.set(config_section_key(section), json_record::dump_record(value)))
    {
        result.status = RepositoryStatus::RedisError;
        result.error = "Redis error";
        return result;
    }
    return result;
}

bool ConfigRepository::save_config(ConfigSection section, const std::string &json_text)
{
    return _redis.set(config_section_key(section), json_text);
}

} // namespace navcaster::storage
