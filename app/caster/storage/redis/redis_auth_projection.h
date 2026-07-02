#pragma once

#include "domain/auth_policy.h"

namespace navcaster::caster {

struct RedisAuthProjectionSnapshot {
    AuthPolicySnapshot policy;
};

} // namespace navcaster::caster
