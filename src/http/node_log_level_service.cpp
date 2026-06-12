#include "node_log_level_service.h"

#include "controller_helpers.h"

#include <nlohmann/json.hpp>

namespace navcaster::http_api
{

NodeLogLevelResult NodeLogLevelService::set_level(const std::string &target_node_id,
                                                  const std::string &current_node_id,
                                                  const std::string &body_text) const
{
    NodeLogLevelResult result;
    if (target_node_id.empty())
    {
        result.response = error_response(400, "Missing node id");
        return result;
    }

    nlohmann::json body;
    if (!parse_json_body(body_text, body))
    {
        result.response = error_response(400, "Invalid JSON");
        return result;
    }

    std::string level_text = body.value("level", "");
    if (level_text.empty())
    {
        result.response = error_response(400, "Missing level");
        return result;
    }

    if (target_node_id != current_node_id && target_node_id != "self" && target_node_id != "current")
    {
        result.response = error_response(501, "Cross-node log level change not implemented");
        return result;
    }

    const auto level = spdlog::level::from_str(level_text);
    if (level == spdlog::level::off && level_text != "off")
    {
        result.response = error_response(400, "Unknown level");
        return result;
    }

    result.should_apply = true;
    result.level = level;
    result.level_text = level_text;
    result.response = json_response(200, {{"node_id", current_node_id}, {"level", level_text}, {"ok", true}});
    return result;
}

} // namespace navcaster::http_api
