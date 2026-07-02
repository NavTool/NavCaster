#include "cluster_monitor_service.h"

#include "controller_helpers.h"

#include <chrono>
#include <unordered_map>

namespace navcaster::http_api
{
namespace
{
using json = nlohmann::json;

int collect_running_relays(const json &states, std::unordered_map<std::string, int> &by_node)
{
    int total = 0;
    if (!states.is_object())
    {
        return total;
    }

    for (const auto &[uid, value] : states.items())
    {
        (void)uid;
        json item = value;
        if (item.is_string())
        {
            try
            {
                item = json::parse(item.get<std::string>());
            }
            catch (...)
            {
                continue;
            }
        }
        if (!item.is_object() || item.value("state", 0) != 1)
        {
            continue;
        }

        ++total;
        std::string node_uid = item.value("node_uid", std::string());
        if (!node_uid.empty())
        {
            by_node[node_uid]++;
        }
    }
    return total;
}

int count_for_node(const std::unordered_map<std::string, int> &counts, const std::string &uid)
{
    auto it = counts.find(uid);
    return it == counts.end() ? 0 : it->second;
}

double measure_master_read_latency_ms(storage::ClusterMonitorRepository &repo)
{
    auto start = std::chrono::steady_clock::now();
    (void)repo.master_node();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}
} // namespace

nlohmann::json build_cluster_monitor_snapshot(const std::string &master_node,
                                              const nlohmann::json &nodes_raw,
                                              const nlohmann::json &pull_states,
                                              const nlohmann::json &push_states,
                                              long long now_ts,
                                              double redis_latency_ms)
{
    std::unordered_map<std::string, int> pull_by_node;
    std::unordered_map<std::string, int> push_by_node;
    const int total_pull = collect_running_relays(pull_states, pull_by_node);
    const int total_push = collect_running_relays(push_states, push_by_node);

    int total_nodes = 0;
    int online_nodes = 0;
    int total_servers = 0;
    int total_clients = 0;
    double total_cpu = 0.0;
    double total_mem = 0.0;
    double total_send = 0.0;
    double total_recv = 0.0;
    json nodes_array = json::array();

    if (nodes_raw.is_object())
    {
        for (const auto &[uid, node_data] : nodes_raw.items())
        {
            total_nodes++;
            if (!node_data.is_object())
            {
                continue;
            }

            const auto &node_info = node_data;
            const bool is_master = uid == master_node;
            const int mpt = node_info.value("server_count", 0);
            const int usr = node_info.value("client_count", 0);
            const int pull = count_for_node(pull_by_node, uid);
            const int push = count_for_node(push_by_node, uid);
            const int conn = node_info.value("connect_count", 0);
            const double cpu = node_info.value("cpu_usage", 0.0);
            const double mem = node_info.value("mem_usage", 0.0);
            const double send_s = node_info.value("send_speed", 0.0);
            const double recv_s = node_info.value("recv_speed", 0.0);
            const long long send_t = node_info.value("send_total", 0LL);
            const long long recv_t = node_info.value("recv_total", 0LL);
            const long long online_time = node_info.value("online_time", 0LL);
            const long long update_time = node_info.value("update_time", 0LL);
            const long long uptime_sec = online_time > 0 ? now_ts - online_time : 0;

            const bool online = update_time == 0 || now_ts - update_time < 60;
            if (online)
            {
                online_nodes++;
                total_servers += mpt;
                total_clients += usr;
                total_cpu += cpu;
                total_mem += mem;
                total_send += send_s;
                total_recv += recv_s;
            }

            nodes_array.push_back({
                {"uid", uid},
                {"node_name", node_info.value("node_name", "")},
                {"is_master", is_master},
                {"online", online},
                {"cpu", cpu},
                {"mem", mem},
                {"mpt", mpt},
                {"usr", usr},
                {"pull", pull},
                {"push", push},
                {"conn", conn},
                {"send_speed", send_s},
                {"recv_speed", recv_s},
                {"send_total", send_t},
                {"recv_total", recv_t},
                {"set_version", node_info.value("set_version", "")},
                {"tag_version", node_info.value("tag_version", "")},
                {"queue_delay", node_info.value("queue_delay", 0)},
                {"hostname", node_info.value("hostname", "")},
                {"listen_port", node_info.value("listen_port", 0)},
                {"http_port", node_info.value("http_port", 0)},
                {"process_id", node_info.value("process_id", 0LL)},
                {"http_enabled", node_info.value("http_enabled", false)},
                {"online_time", online_time},
                {"update_time", update_time},
                {"uptime_sec", uptime_sec},
                {"pub_ping_delay", node_info.value("pub_ping_delay", 0LL)},
                {"sub_ping_delay", node_info.value("sub_ping_delay", 0LL)}
            });
        }
    }

    return {
        {"master_node", master_node},
        {"total_nodes", total_nodes},
        {"online_nodes", online_nodes},
        {"total_servers", total_servers},
        {"total_clients", total_clients},
        {"total_pull", total_pull},
        {"total_push", total_push},
        {"total_cpu", total_cpu},
        {"total_mem", total_mem},
        {"total_send_speed", total_send},
        {"total_recv_speed", total_recv},
        {"redis_latency_ms", redis_latency_ms},
        {"nodes", nodes_array}};
}

ClusterMonitorService::ClusterMonitorService(storage::RedisHashClient &redis)
    : _redis(redis)
{
}

ControllerResponse ClusterMonitorService::snapshot(long long now_ts)
{
    storage::ClusterMonitorRepository repo(_redis);
    auto body = build_cluster_monitor_snapshot(repo.master_node(),
                                               repo.nodes(),
                                               repo.pull_states(),
                                               repo.push_states(),
                                               now_ts,
                                               measure_master_read_latency_ms(repo));
    return json_response(200, body);
}

} // namespace navcaster::http_api
