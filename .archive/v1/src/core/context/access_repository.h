#pragma once

#include "redis_hash_client.h"
#include "repository_status.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

struct AccessRepositoryResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    std::string uid;
    std::string group_uid;
    std::string mountpoint;
    std::string error;
    nlohmann::json record;
};

bool build_access_group_plan(nlohmann::json group, std::int64_t now, AccessRepositoryResult &plan, std::string *error = nullptr);
bool build_access_item_plan(const std::string &group_uid, nlohmann::json item, AccessRepositoryResult &plan, std::string *error = nullptr);

class AccessRepository
{
public:
    explicit AccessRepository(RedisHashClient &redis);

    void ensure_builtin_groups(std::int64_t now);

    nlohmann::json list_groups();
    nlohmann::json get_group(const std::string &uid);
    AccessRepositoryResult create_group(nlohmann::json group, std::int64_t now);
    AccessRepositoryResult update_group(const std::string &uid, nlohmann::json group, std::int64_t now);
    AccessRepositoryResult delete_group(const std::string &uid);

    nlohmann::json list_items(const std::string &group_uid);
    AccessRepositoryResult create_item(const std::string &group_uid, nlohmann::json item);
    AccessRepositoryResult update_item(const std::string &group_uid, nlohmann::json item);
    AccessRepositoryResult delete_item(const std::string &group_uid, const std::string &mountpoint);

private:
    bool publish_changed();

    RedisHashClient &_redis;
};

} // namespace navcaster::storage
