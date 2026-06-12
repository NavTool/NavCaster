#pragma once

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace navcaster::core
{
inline constexpr int NODE_HISTORY_RAW_TRIM_MAX = 120960;
inline constexpr int NODE_HISTORY_1M_TRIM_MAX = 43200;
inline constexpr int NODE_HISTORY_5M_TRIM_MAX = 8640;

struct NodeHistoryWrite
{
    std::string key;
    std::string value;
    int trim_max = 0;
};

class NodeHistoryRecorder
{
public:
    explicit NodeHistoryRecorder(std::string node_id = std::string());

    void set_node_id(std::string node_id);
    std::vector<NodeHistoryWrite> record(const nlohmann::json &node_json, long long timestamp);

private:
    static nlohmann::json build_snapshot(const nlohmann::json &node_json, long long timestamp);
    static nlohmann::json aggregate(const std::deque<nlohmann::json> &samples);

    std::string _node_id;
    std::deque<nlohmann::json> _one_minute_buffer;
    std::deque<nlohmann::json> _five_minute_buffer;
    int _one_minute_counter = 0;
    int _five_minute_counter = 0;
};
} // namespace navcaster::core
