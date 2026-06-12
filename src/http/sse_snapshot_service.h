#pragma once

#include "redis_hash_client.h"

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

class SseSnapshotService
{
public:
    SseSnapshotService(storage::RedisHashClient &caster_redis, storage::RedisHashClient &auth_redis);

    nlohmann::json servers();
    nlohmann::json clients();
    nlohmann::json streams();
    nlohmann::json nodes();
    nlohmann::json accounts();
    nlohmann::json account_actives();
    nlohmann::json sources();
    nlohmann::json aliases();
    nlohmann::json access_groups();
    nlohmann::json pull_records();
    nlohmann::json pull_states();
    nlohmann::json push_records();
    nlohmann::json push_states();

    template <typename SseManager>
    void register_channels(SseManager &sse)
    {
        sse.register_channel("servers", [this]() -> nlohmann::json { return servers(); });
        sse.register_channel("clients", [this]() -> nlohmann::json { return clients(); });
        sse.register_channel("streams", [this]() -> nlohmann::json { return streams(); });
        sse.register_channel("nodes", [this]() -> nlohmann::json { return nodes(); });
        sse.register_channel("accounts", [this]() -> nlohmann::json { return accounts(); });
        sse.register_channel("sources", [this]() -> nlohmann::json { return sources(); });
        sse.register_channel("aliases", [this]() -> nlohmann::json { return aliases(); });
        sse.register_channel("access_groups", [this]() -> nlohmann::json { return access_groups(); });
        sse.register_channel("pull_records", [this]() -> nlohmann::json { return pull_records(); });
        sse.register_channel("pull_states", [this]() -> nlohmann::json { return pull_states(); });
        sse.register_channel("push_records", [this]() -> nlohmann::json { return push_records(); });
        sse.register_channel("push_states", [this]() -> nlohmann::json { return push_states(); });
        sse.register_channel("account_actives", [this]() -> nlohmann::json { return account_actives(); });
    }

private:
    storage::RedisHashClient &_caster_redis;
    storage::RedisHashClient &_auth_redis;
};

} // namespace navcaster::http_api
