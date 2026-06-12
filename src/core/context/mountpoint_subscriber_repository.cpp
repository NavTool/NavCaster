#include "mountpoint_subscriber_repository.h"

#include "redis_keys.h"

namespace navcaster::storage
{

MountpointSubscriberRepository::MountpointSubscriberRepository(RedisHashClient &redis)
    : _redis(redis)
{
}

nlohmann::json MountpointSubscriberRepository::online_mountpoints()
{
    return _redis.hgetall(redis_keys::MPT_LIST);
}

long long MountpointSubscriberRepository::subscriber_count(const std::string &mountpoint)
{
    if (mountpoint.empty())
    {
        return 0;
    }
    const auto key = redis_keys::mpt_sub(mountpoint);
    return _redis.hlen(key.c_str());
}

} // namespace navcaster::storage
