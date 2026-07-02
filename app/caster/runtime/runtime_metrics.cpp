#include "runtime/runtime_metrics.h"

#include <sstream>
#include <string>

namespace navcaster::caster {
namespace {

void append_bool(std::ostringstream &out, bool value)
{
    out << (value ? "true" : "false");
}

void append_json_string(std::ostringstream &out, const std::string &value)
{
    out << "\"";
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    out << "\"";
}

} // namespace

std::string runtime_metrics_to_json(const RuntimeMetricsSnapshot &snapshot)
{
    WorkerMetricsSnapshot totals;
    for (const auto &worker : snapshot.workers) {
        totals.mailbox_messages += worker.mailbox_messages;
        totals.handoff_received += worker.handoff_received;
        totals.active_sessions += worker.active_sessions;
        totals.active_mounts += worker.active_mounts;
        totals.source_count += worker.source_count;
        totals.client_count += worker.client_count;
        totals.bytes_in += worker.bytes_in;
        totals.bytes_out += worker.bytes_out;
        totals.fanout_write_count += worker.fanout_write_count;
        totals.redis_publish_count += worker.redis_publish_count;
        totals.redis_publish_bytes += worker.redis_publish_bytes;
        totals.redis_publish_error_count += worker.redis_publish_error_count;
        totals.redis_subscribe_message_count += worker.redis_subscribe_message_count;
        totals.redis_subscribe_bytes += worker.redis_subscribe_bytes;
        totals.redis_remote_fanout_write_count += worker.redis_remote_fanout_write_count;
        totals.redis_remote_fanout_bytes += worker.redis_remote_fanout_bytes;
        totals.redis_error_count += worker.redis_error_count;
        totals.redis_subscribed_mount_count += worker.redis_subscribed_mount_count;
        totals.slow_client_disconnect_count += worker.slow_client_disconnect_count;
        totals.output_buffer_limit_count += worker.output_buffer_limit_count;
        totals.redis_contexts_reserved += worker.redis_contexts_reserved;
    }

    std::ostringstream out;
    out << "{";
    out << "\"runtime_id\":";
    append_json_string(out, snapshot.runtime_id);
    out << ",";
    out << "\"running\":";
    append_bool(out, snapshot.running);
    out << ",\"uptime_ms\":" << snapshot.uptime_ms;
    out << ",\"worker_count\":" << snapshot.worker_count;
    out << ",\"mount_count\":" << snapshot.mount_count;
    out << ",\"connection_count\":" << totals.active_sessions;
    out << ",\"source_count\":" << totals.source_count;
    out << ",\"client_count\":" << totals.client_count;
    out << ",\"bytes_in\":" << totals.bytes_in;
    out << ",\"bytes_out\":" << totals.bytes_out;
    out << ",\"fanout_write_count\":" << totals.fanout_write_count;
    out << ",\"redis_publish_count\":" << totals.redis_publish_count;
    out << ",\"redis_publish_bytes\":" << totals.redis_publish_bytes;
    out << ",\"redis_publish_error_count\":" << totals.redis_publish_error_count;
    out << ",\"redis_subscribe_message_count\":" << totals.redis_subscribe_message_count;
    out << ",\"redis_subscribe_bytes\":" << totals.redis_subscribe_bytes;
    out << ",\"redis_remote_fanout_write_count\":" << totals.redis_remote_fanout_write_count;
    out << ",\"redis_remote_fanout_bytes\":" << totals.redis_remote_fanout_bytes;
    out << ",\"redis_subscribed_mount_count\":" << totals.redis_subscribed_mount_count;
    out << ",\"redis_error_count\":" << totals.redis_error_count;
    out << ",\"slow_client_disconnect_count\":" << totals.slow_client_disconnect_count;
    out << ",\"output_buffer_limit_count\":" << totals.output_buffer_limit_count;
    out << ",\"mount_owners\":[";
    for (std::size_t i = 0; i < snapshot.mount_owners.size(); ++i) {
        const auto &owner = snapshot.mount_owners[i];
        if (i > 0) {
            out << ",";
        }
        out << "{";
        out << "\"mount\":";
        append_json_string(out, owner.mount);
        out << ",\"worker_id\":" << owner.worker_id;
        out << ",\"draining\":";
        append_bool(out, owner.draining);
        out << "}";
    }
    out << "]";
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
        out << ",\"redis_connected\":";
        append_bool(out, worker.redis_connected);
        out << ",\"redis_contexts_reserved\":" << worker.redis_contexts_reserved;
        out << "}";
    }
    out << "]}";
    return out.str();
}

} // namespace navcaster::caster
