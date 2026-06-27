#include "runtime/runtime_metrics.h"

#include <sstream>

namespace navcaster::caster {
namespace {

void append_bool(std::ostringstream &out, bool value)
{
    out << (value ? "true" : "false");
}

} // namespace

std::string runtime_metrics_to_json(const RuntimeMetricsSnapshot &snapshot)
{
    std::ostringstream out;
    out << "{";
    out << "\"runtime_id\":\"" << snapshot.runtime_id << "\",";
    out << "\"running\":";
    append_bool(out, snapshot.running);
    out << ",\"uptime_ms\":" << snapshot.uptime_ms;
    out << ",\"worker_count\":" << snapshot.worker_count;
    out << ",\"mount_count\":" << snapshot.mount_count;
    out << ",\"workers\":[";
    for (std::size_t i = 0; i < snapshot.workers.size(); ++i) {
        const auto &worker = snapshot.workers[i];
        if (i > 0) {
            out << ",";
        }
        out << "{";
        out << "\"worker_id\":" << worker.worker_id;
        out << ",\"running\":";
        append_bool(out, worker.running);
        out << ",\"draining\":";
        append_bool(out, worker.draining);
        out << ",\"mailbox_messages\":" << worker.mailbox_messages;
        out << ",\"handoff_received\":" << worker.handoff_received;
        out << ",\"active_sessions\":" << worker.active_sessions;
        out << ",\"active_mounts\":" << worker.active_mounts;
        out << ",\"source_count\":" << worker.source_count;
        out << ",\"client_count\":" << worker.client_count;
        out << ",\"bytes_in\":" << worker.bytes_in;
        out << ",\"bytes_out\":" << worker.bytes_out;
        out << ",\"fanout_write_count\":" << worker.fanout_write_count;
        out << ",\"redis_publish_count\":" << worker.redis_publish_count;
        out << ",\"redis_publish_bytes\":" << worker.redis_publish_bytes;
        out << ",\"redis_publish_error_count\":" << worker.redis_publish_error_count;
        out << ",\"redis_subscribe_message_count\":" << worker.redis_subscribe_message_count;
        out << ",\"redis_subscribe_bytes\":" << worker.redis_subscribe_bytes;
        out << ",\"redis_remote_fanout_write_count\":" << worker.redis_remote_fanout_write_count;
        out << ",\"redis_remote_fanout_bytes\":" << worker.redis_remote_fanout_bytes;
        out << ",\"redis_error_count\":" << worker.redis_error_count;
        out << ",\"redis_subscribed_mount_count\":" << worker.redis_subscribed_mount_count;
        out << ",\"slow_client_disconnect_count\":" << worker.slow_client_disconnect_count;
        out << ",\"output_buffer_limit_count\":" << worker.output_buffer_limit_count;
        out << ",\"redis_contexts_reserved\":" << worker.redis_contexts_reserved;
        out << "}";
    }
    out << "]}";
    return out.str();
}

} // namespace navcaster::caster
