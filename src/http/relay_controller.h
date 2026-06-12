#pragma once

#include "controller_response.h"
#include "redis_hash_client.h"
#include "relay_repository.h"

#include <string>

namespace navcaster::http_api
{

class RelayController
{
public:
    explicit RelayController(storage::RedisHashClient &redis);

    ControllerResponse list_records(storage::RelayKind kind);
    ControllerResponse get_record(storage::RelayKind kind, const std::string &uid);
    ControllerResponse create_record(storage::RelayKind kind, const std::string &body_text);
    ControllerResponse update_record(storage::RelayKind kind, const std::string &uid, const std::string &body_text);
    ControllerResponse delete_record(storage::RelayKind kind, const std::string &uid);
    ControllerResponse list_states(storage::RelayKind kind);
    ControllerResponse set_enabled(storage::RelayKind kind, const std::string &uid, bool enabled);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
