#pragma once

#include "redis_hash_client.h"
#include "repository_status.h"

#include <string>

#include <nlohmann/json.hpp>

namespace navcaster::storage
{

enum class RelayKind
{
    Pull,
    Push
};

struct RelayRepositoryResult
{
    RepositoryStatus status = RepositoryStatus::Ok;
    RelayKind kind = RelayKind::Pull;
    std::string uid;
    std::string error;
    nlohmann::json record;
};

const char *relay_record_key(RelayKind kind);
const char *relay_state_key(RelayKind kind);
bool build_relay_record_plan(RelayKind kind, nlohmann::json record, RelayRepositoryResult &plan, std::string *error = nullptr);

class RelayRepository
{
public:
    explicit RelayRepository(RedisHashClient &redis);

    nlohmann::json list_records(RelayKind kind);
    nlohmann::json get_record(RelayKind kind, const std::string &uid);
    nlohmann::json list_states(RelayKind kind);

    RelayRepositoryResult create_record(RelayKind kind, nlohmann::json record);
    RelayRepositoryResult update_record(RelayKind kind, const std::string &uid, nlohmann::json record);
    RelayRepositoryResult delete_record(RelayKind kind, const std::string &uid);
    RelayRepositoryResult set_enabled(RelayKind kind, const std::string &uid, bool enabled);

private:
    RedisHashClient &_redis;
};

} // namespace navcaster::storage
