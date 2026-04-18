#pragma once
#include "context_util.h"
#include "SysUsage.h"
#include "version.h"

#include <sys/utsname.h>

inline std::string resolve_node_platform()
{
    std::string platform = SYSTEM_PLATFORM;
    if (!platform.empty())
        return platform;

    struct utsname info;
    if (uname(&info) == 0)
        return std::string(info.sysname) + "-" + info.machine;

    return "unknown";
}

// 节点状态信息
class caster_node
{
private:
    std::string _uid;
    std::string _node_name;

    std::time_t _online_time = 0;
    std::time_t _update_time = 0;

    double _cpu_usage = 0;
    double _mem_usage = 0;
    int64_t _queue_delay = 0;
    int64_t _sub_ping_delay = 0;
    int64_t _sub_tcp_delay = 0;
    int64_t _pub_ping_delay = 0;
    int64_t _pub_tcp_delay = 0;

    double _send_total = 0;
    double _send_speed = 0;
    double _recv_total = 0;
    double _recv_speed = 0;

    size_t _connect_count = 0;
    size_t _server_count = 0;
    size_t _client_count = 0;
    uint32_t _listen_port = 0;
    uint32_t _http_port = 0;
    uint32_t _process_id = 0;

public:
    caster_node(std::string uid, std::string node_name, std::time_t online_time = 0)
    {
        _uid = uid;
        _node_name = node_name;
        _online_time = online_time ? online_time : util_get_now_second();
    }

    int set_sys_usage()
    {
        _cpu_usage = SysUsage::getInstance()->getProcessCPU();
        _mem_usage = SysUsage::getInstance()->getProcessMemory();
        return 0;
    }

    int set_delay_info(int64_t queue_delay, int64_t sub_ping_delay, int64_t sub_tcp_delay, int64_t pub_ping_delay, int64_t pub_tcp_delay)
    {
        _queue_delay = queue_delay;
        _sub_ping_delay = sub_ping_delay;
        _sub_tcp_delay = sub_tcp_delay;
        _pub_ping_delay = pub_ping_delay;
        _pub_tcp_delay = pub_tcp_delay;
        return 0;
    }

    int set_traffic_info(double send_total, double send_speed, double recv_total, double recv_speed)
    {
        _send_total = send_total;
        _send_speed = send_speed;
        _recv_total = recv_total;
        _recv_speed = recv_speed;
        return 0;
    }

    int set_connection_count(size_t server_count, size_t client_count)
    {
        _server_count = server_count;
        _client_count = client_count;
        _connect_count = server_count + client_count;
        return 0;
    }

    int set_runtime_info(uint32_t listen_port, uint32_t http_port, uint32_t process_id)
    {
        _listen_port = listen_port;
        _http_port = http_port;
        _process_id = process_id;
        return 0;
    }

    int fromString(const std::string &str)
    {
        caster::core::CasterNode proto;
        if (!JsonToProto(str, proto))
            return 1;
        _uid = proto.uid();
        _node_name = proto.node_name();
        _cpu_usage = proto.cpu_usage();
        _mem_usage = proto.mem_usage();
        _queue_delay = proto.queue_delay();
        _sub_ping_delay = proto.sub_ping_delay();
        _sub_tcp_delay = proto.sub_tcp_delay();
        _pub_ping_delay = proto.pub_ping_delay();
        _pub_tcp_delay = proto.pub_tcp_delay();
        _send_total = proto.send_total();
        _send_speed = proto.send_speed();
        _recv_total = proto.recv_total();
        _recv_speed = proto.recv_speed();
        _connect_count = proto.connect_count();
        _server_count = proto.server_count();
        _client_count = proto.client_count();
        _online_time = proto.online_time();
        _update_time = proto.update_time();

        try
        {
            auto extra = nlohmann::json::parse(str);
            _listen_port = extra.value("listen_port", 0U);
            _http_port = extra.value("http_port", 0U);
            _process_id = extra.value("process_id", 0U);
        }
        catch (...)
        {
            _listen_port = 0;
            _http_port = 0;
            _process_id = 0;
        }
        return 0;
    }

    std::string toString()
    {
        _update_time = util_get_now_second();

        caster::core::CasterNode proto;

        proto.set_uid(_uid);
        proto.set_node_name(_node_name);

        proto.set_set_version(PROJECT_SET_VERSION);
        proto.set_tag_version(PROJECT_TAG_VERSION);
        proto.set_run_platform(resolve_node_platform());

        proto.set_cpu_usage(_cpu_usage);
        proto.set_mem_usage(_mem_usage);
        proto.set_queue_delay(_queue_delay);
        proto.set_sub_ping_delay(_sub_ping_delay);
        proto.set_sub_tcp_delay(_sub_tcp_delay);
        proto.set_pub_ping_delay(_pub_ping_delay);
        proto.set_pub_tcp_delay(_pub_tcp_delay);

        proto.set_send_total(_send_total);
        proto.set_send_speed(_send_speed);
        proto.set_recv_total(_recv_total);
        proto.set_recv_speed(_recv_speed);

        proto.set_connect_count(_connect_count);
        proto.set_server_count(_server_count);
        proto.set_client_count(_client_count);

        proto.set_online_time(_online_time);
        proto.set_update_time(_update_time);

        auto result = nlohmann::json::parse(ProtoToJson(proto));
        result["listen_port"] = _listen_port;
        result["http_port"] = _http_port;
        result["process_id"] = _process_id;
        return result.dump();
    }
};
