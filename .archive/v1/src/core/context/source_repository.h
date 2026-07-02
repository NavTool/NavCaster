#pragma once

#include "redis_hash_client.h"
#include "repository_status.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

struct SourceRepositoryResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    std::string mountpoint;
    std::string error;
    nlohmann::json record;
};

bool build_source_create_plan(nlohmann::json record, std::int64_t now, SourceRepositoryResult &plan, std::string *error = nullptr);
bool build_source_update_plan(const std::string &mountpoint, nlohmann::json record, std::int64_t now, SourceRepositoryResult &plan, std::string *error = nullptr);

class SourceRepository
{
public:
    explicit SourceRepository(RedisHashClient &redis);

    nlohmann::json list_sources();
    nlohmann::json get_source(const std::string &mountpoint);
    SourceRepositoryResult create_source(nlohmann::json record, std::int64_t now);
    SourceRepositoryResult update_source(const std::string &mountpoint, nlohmann::json record, std::int64_t now);
    SourceRepositoryResult delete_source(const std::string &mountpoint);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
