#include "storage/redis/redis_pubsub.h"

namespace navcaster::caster {

WorkerRedisBoundary::WorkerRedisBoundary(const std::string &host, int port)
    : command("worker-command", host, port), pubsub("worker-pubsub", host, port)
{
}

} // namespace navcaster::caster
