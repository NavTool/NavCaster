#pragma once

#include <string>

namespace navcaster::caster {

struct AuthPolicySnapshot {
    std::string version = "empty";
    bool allow_anonymous = false;
};

} // namespace navcaster::caster
