#pragma once

namespace navcaster::storage
{

enum class RepositoryStatus
{
    Ok,
    Invalid,
    NotFound,
    Conflict,
    RedisError
};

} // namespace navcaster::storage
