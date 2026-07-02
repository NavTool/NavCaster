#pragma once

#include <string>

#include "runtime/runtime_config.h"

namespace navcaster::caster {

struct ConfigLoadResult {
    RuntimeConfig config;
    bool ok = true;
    std::string error;
};

ConfigLoadResult load_runtime_config(int argc, char **argv);
std::string runtime_usage();

} // namespace navcaster::caster
