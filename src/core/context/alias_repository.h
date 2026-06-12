#pragma once

#include "redis_hash_client.h"
#include "repository_status.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

struct AliasRepositoryResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    std::string uid;
    std::string error;
    nlohmann::json rule;
};

bool build_alias_create_plan(nlohmann::json rule, std::int64_t now, AliasRepositoryResult &plan, std::string *error = nullptr);
bool build_alias_update_plan(const std::string &uid, nlohmann::json rule, std::int64_t now, AliasRepositoryResult &plan, std::string *error = nullptr);

class AliasRepository
{
public:
    explicit AliasRepository(RedisHashClient &redis);

    nlohmann::json list_aliases();
    nlohmann::json get_alias(const std::string &uid);
    AliasRepositoryResult create_alias(nlohmann::json rule, std::int64_t now);
    AliasRepositoryResult update_alias(const std::string &uid, nlohmann::json rule, std::int64_t now);
    AliasRepositoryResult delete_alias(const std::string &uid);

private:
    bool publish_changed();

    RedisHashClient &_redis;
};

} // namespace navcaster::storage
