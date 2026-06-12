#include "node_history_recorder.h"

#include "redis_keys.h"

namespace navcaster::core
{
NodeHistoryRecorder::NodeHistoryRecorder(std::string node_id)
    : _node_id(std::move(node_id))
{
}

void NodeHistoryRecorder::set_node_id(std::string node_id)
{
    _node_id = std::move(node_id);
}

std::vector<NodeHistoryWrite> NodeHistoryRecorder::record(const nlohmann::json &node_json, long long timestamp)
{
    std::vector<NodeHistoryWrite> writes;
    const auto snapshot = build_snapshot(node_json, timestamp);
    const std::string raw_key = redis_keys::node_history(_node_id);

    writes.push_back({raw_key, snapshot.dump(), NODE_HISTORY_RAW_TRIM_MAX});

    _one_minute_buffer.push_back(snapshot);
    if (++_one_minute_counter >= 12)
    {
        _one_minute_counter = 0;
        auto one_minute = aggregate(_one_minute_buffer);
        writes.push_back({redis_keys::node_history_1m(_node_id), one_minute.dump(), NODE_HISTORY_1M_TRIM_MAX});

        _five_minute_buffer.push_back(one_minute);
        if (++_five_minute_counter >= 5)
        {
            _five_minute_counter = 0;
            auto five_minute = aggregate(_five_minute_buffer);
            writes.push_back({redis_keys::node_history_5m(_node_id), five_minute.dump(), NODE_HISTORY_5M_TRIM_MAX});
            _five_minute_buffer.clear();
        }
        _one_minute_buffer.clear();
    }

    return writes;
}

nlohmann::json NodeHistoryRecorder::build_snapshot(const nlohmann::json &node_json, long long timestamp)
{
    nlohmann::json snapshot;
    snapshot["t"] = timestamp;
    snapshot["cpu"] = node_json.value("cpu_usage", 0.0);
    snapshot["mem"] = node_json.value("mem_usage", 0.0);
    snapshot["mpt"] = node_json.value("server_count", 0);
    snapshot["usr"] = node_json.value("client_count", 0);
    snapshot["pull"] = node_json.value("pull_count", 0);
    snapshot["push"] = node_json.value("push_count", 0);
    snapshot["conn"] = node_json.value("connect_count", 0);
    snapshot["send_speed"] = node_json.value("send_speed", 0.0);
    snapshot["recv_speed"] = node_json.value("recv_speed", 0.0);
    snapshot["send_total"] = node_json.value("send_total", 0LL);
    snapshot["recv_total"] = node_json.value("recv_total", 0LL);
    snapshot["q_delay"] = node_json.value("queue_delay", 0.0);
    return snapshot;
}

nlohmann::json NodeHistoryRecorder::aggregate(const std::deque<nlohmann::json> &samples)
{
    nlohmann::json agg;
    double cpu_sum = 0.0;
    double mem_sum = 0.0;
    double delay_sum = 0.0;
    double send_speed_sum = 0.0;
    double recv_speed_sum = 0.0;
    int mpt_sum = 0;
    int usr_sum = 0;
    int pull_sum = 0;
    int push_sum = 0;
    int conn_sum = 0;
    long long last_send_total = 0;
    long long last_recv_total = 0;
    long long time_sum = 0;
    const int count = static_cast<int>(samples.size());

    for (const auto &sample : samples)
    {
        time_sum += sample["t"].get<long long>();
        cpu_sum += sample["cpu"].get<double>();
        mem_sum += sample["mem"].get<double>();
        mpt_sum += sample["mpt"].get<int>();
        usr_sum += sample["usr"].get<int>();
        pull_sum += sample.value("pull", 0);
        push_sum += sample.value("push", 0);
        conn_sum += sample["conn"].get<int>();
        send_speed_sum += sample["send_speed"].get<double>();
        recv_speed_sum += sample["recv_speed"].get<double>();
        last_send_total = sample["send_total"].get<long long>();
        last_recv_total = sample["recv_total"].get<long long>();
        delay_sum += sample["q_delay"].get<double>();
    }

    agg["t"] = time_sum / count;
    agg["cpu"] = cpu_sum / count;
    agg["mem"] = mem_sum / count;
    agg["mpt"] = mpt_sum / count;
    agg["usr"] = usr_sum / count;
    agg["pull"] = pull_sum / count;
    agg["push"] = push_sum / count;
    agg["conn"] = conn_sum / count;
    agg["send_speed"] = send_speed_sum / count;
    agg["recv_speed"] = recv_speed_sum / count;
    agg["send_total"] = last_send_total;
    agg["recv_total"] = last_recv_total;
    agg["q_delay"] = delay_sum / count;
    return agg;
}
} // namespace navcaster::core
