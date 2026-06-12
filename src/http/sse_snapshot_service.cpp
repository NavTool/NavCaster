#include "sse_snapshot_service.h"

#include "access_repository.h"
#include "account_repository.h"
#include "alias_repository.h"
#include "relay_repository.h"
#include "runtime_state_repository.h"
#include "source_repository.h"

namespace navcaster::http_api
{

SseSnapshotService::SseSnapshotService(storage::RedisHashClient &caster_redis, storage::RedisHashClient &auth_redis)
    : _caster_redis(caster_redis), _auth_redis(auth_redis)
{
}

nlohmann::json SseSnapshotService::servers()
{
    storage::RuntimeStateRepository repo(_caster_redis);
    return repo.list(storage::RuntimeStateKind::Server);
}

nlohmann::json SseSnapshotService::clients()
{
    storage::RuntimeStateRepository repo(_caster_redis);
    return repo.list(storage::RuntimeStateKind::Client);
}

nlohmann::json SseSnapshotService::streams()
{
    storage::RuntimeStateRepository repo(_caster_redis);
    return repo.list(storage::RuntimeStateKind::Stream);
}

nlohmann::json SseSnapshotService::nodes()
{
    storage::RuntimeStateRepository repo(_caster_redis);
    return repo.list(storage::RuntimeStateKind::Node);
}

nlohmann::json SseSnapshotService::accounts()
{
    storage::AccountRepository repo(_auth_redis);
    return repo.list_accounts();
}

nlohmann::json SseSnapshotService::account_actives()
{
    storage::AccountRepository repo(_auth_redis);
    return repo.list_legacy_active_sessions();
}

nlohmann::json SseSnapshotService::sources()
{
    storage::SourceRepository repo(_caster_redis);
    return repo.list_sources();
}

nlohmann::json SseSnapshotService::aliases()
{
    storage::AliasRepository repo(_caster_redis);
    return repo.list_aliases();
}

nlohmann::json SseSnapshotService::access_groups()
{
    storage::AccessRepository repo(_caster_redis);
    return repo.list_groups();
}

nlohmann::json SseSnapshotService::pull_records()
{
    storage::RelayRepository repo(_caster_redis);
    return repo.list_records(storage::RelayKind::Pull);
}

nlohmann::json SseSnapshotService::pull_states()
{
    storage::RelayRepository repo(_caster_redis);
    return repo.list_states(storage::RelayKind::Pull);
}

nlohmann::json SseSnapshotService::push_records()
{
    storage::RelayRepository repo(_caster_redis);
    return repo.list_records(storage::RelayKind::Push);
}

nlohmann::json SseSnapshotService::push_states()
{
    storage::RelayRepository repo(_caster_redis);
    return repo.list_states(storage::RelayKind::Push);
}

} // namespace navcaster::http_api
