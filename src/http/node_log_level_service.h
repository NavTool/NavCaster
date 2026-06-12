#pragma once

#include "controller_response.h"

#include <string>

#include <spdlog/common.h>

namespace navcaster::http_api
{

struct NodeLogLevelResult
{
    ControllerResponse response;
    bool should_apply = false;
    spdlog::level::level_enum level = spdlog::level::off;
    std::string level_text;
};

class NodeLogLevelService
{
public:
    NodeLogLevelResult set_level(const std::string &target_node_id,
                                 const std::string &current_node_id,
                                 const std::string &body_text) const;
};

} // namespace navcaster::http_api
