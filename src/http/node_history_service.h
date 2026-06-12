#pragma once

#include "controller_response.h"
#include "node_history_repository.h"
#include "redis_hash_client.h"

#include <string>

namespace navcaster::http_api
{

class NodeHistoryService
{
public:
    explicit NodeHistoryService(storage::RedisHashClient &redis);

    ControllerResponse list(const std::string &node_id, const std::string &range, long long limit);

private:
    storage::RedisHashClient &_redis;
};

} // namespace navcaster::http_api
