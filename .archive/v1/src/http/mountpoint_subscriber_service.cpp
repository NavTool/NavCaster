#include "mountpoint_subscriber_service.h"

#include "controller_helpers.h"
#include "mountpoint_subscriber_repository.h"

namespace navcaster::http_api
{

MountpointSubscriberService::MountpointSubscriberService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse MountpointSubscriberService::list()
{
    storage::MountpointSubscriberRepository repo(_redis);
    nlohmann::json result = nlohmann::json::object();
    auto mountpoints = repo.online_mountpoints();
    if (mountpoints.is_object())
    {
        for (const auto &[mountpoint, value] : mountpoints.items())
        {
            (void)value;
            result[mountpoint] = repo.subscriber_count(mountpoint);
        }
    }
    return json_response(200, result);
}

} // namespace navcaster::http_api
