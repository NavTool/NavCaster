#include "connection_history_service.h"

#include "controller_helpers.h"

namespace navcaster::http_api
{

ConnectionHistoryService::ConnectionHistoryService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse ConnectionHistoryService::list(storage::ConnectionHistoryKind kind)
{
    storage::ConnectionHistoryRepository repo(_redis);
    return json_response(200, repo.list(kind));
}

} // namespace navcaster::http_api
