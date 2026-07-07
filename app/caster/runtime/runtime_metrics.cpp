#include "runtime/runtime_metrics.h"

#include <nlohmann/json.hpp>

namespace navcaster::caster {
namespace {

using Json = nlohmann::ordered_json;

Json position_to_json(const GeoPosition &position)
{
    return Json{
        {"valid", position.valid},
        {"latitude_deg", position.latitude_deg},
        {"longitude_deg", position.longitude_deg},
        {"height_m", position.height_m},
        {"ecef_x_m", position.ecef_x_m},
        {"ecef_y_m", position.ecef_y_m},
        {"ecef_z_m", position.ecef_z_m},
        {"updated_at_ms", position.updated_at_ms},
    };
}

Json mount_to_json(const MountMetricsSnapshot &mount)
{
    return Json{
        {"worker_id", mount.worker_id},
        {"mount", mount.mount},
        {"server_online", mount.server_online},
        {"client_count", mount.client_count},
        {"server_bytes_in", mount.server_bytes_in},
        {"server_rtcm_frame_count", mount.server_rtcm_frame_count},
        {"base_position_report_count", mount.base_position_report_count},
        {"base_position_source", position_source_name(mount.base_position_source)},
        {"base_position", position_to_json(mount.base_position)},
    };
}

Json client_to_json(const ClientMetricsSnapshot &client)
{
    return Json{
        {"worker_id", client.worker_id},
        {"session_id", client.session_id},
        {"member_key", client.member_key},
        {"mount", client.mount},
        {"remote_addr", client.remote_addr},
        {"remote_port", client.remote_port},
        {"position_report_count", client.position_report_count},
        {"position_source", position_source_name(client.position_source)},
        {"position", position_to_json(client.position)},
    };
}

WorkerMetricsSnapshot totals_for(const std::vector<WorkerMetricsSnapshot> &workers)
{
    WorkerMetricsSnapshot totals;
    for (const auto &worker : workers) {
        totals.mailbox_messages += worker.mailbox_messages;
        totals.handoff_received += worker.handoff_received;
        totals.active_sessions += worker.active_sessions;
        totals.active_mounts += worker.active_mounts;
        totals.server_count += worker.server_count;
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
        totals.redis_position_report_count += worker.redis_position_report_count;
        totals.redis_position_report_error_count += worker.redis_position_report_error_count;
        totals.slow_client_disconnect_count += worker.slow_client_disconnect_count;
        totals.output_buffer_limit_count += worker.output_buffer_limit_count;
        totals.redis_contexts_reserved += worker.redis_contexts_reserved;
    }
    return totals;
}

Json mount_owners_to_json(const std::vector<MountOwnerSnapshot> &owners)
{
    auto items = Json::array();
    for (const auto &owner : owners) {
        items.push_back(Json{
            {"mount", owner.mount},
            {"worker_id", owner.worker_id},
            {"draining", owner.draining},
        });
    }
    return items;
}

Json worker_to_json(const WorkerMetricsSnapshot &worker)
{
    auto mounts = Json::array();
    for (const auto &mount : worker.mounts) {
        mounts.push_back(mount_to_json(mount));
    }

    auto clients = Json::array();
    for (const auto &client : worker.clients) {
        clients.push_back(client_to_json(client));
    }

    return Json{
        {"worker_id", worker.worker_id},
        {"running", worker.running},
        {"draining", worker.draining},
        {"mailbox_messages", worker.mailbox_messages},
        {"handoff_received", worker.handoff_received},
        {"active_sessions", worker.active_sessions},
        {"active_mounts", worker.active_mounts},
        {"server_count", worker.server_count},
        {"client_count", worker.client_count},
        {"bytes_in", worker.bytes_in},
        {"bytes_out", worker.bytes_out},
        {"fanout_write_count", worker.fanout_write_count},
        {"redis_publish_count", worker.redis_publish_count},
        {"redis_publish_bytes", worker.redis_publish_bytes},
        {"redis_publish_error_count", worker.redis_publish_error_count},
        {"redis_subscribe_message_count", worker.redis_subscribe_message_count},
        {"redis_subscribe_bytes", worker.redis_subscribe_bytes},
        {"redis_remote_fanout_write_count", worker.redis_remote_fanout_write_count},
        {"redis_remote_fanout_bytes", worker.redis_remote_fanout_bytes},
        {"redis_error_count", worker.redis_error_count},
        {"redis_subscribed_mount_count", worker.redis_subscribed_mount_count},
        {"redis_position_report_count", worker.redis_position_report_count},
        {"redis_position_report_error_count", worker.redis_position_report_error_count},
        {"slow_client_disconnect_count", worker.slow_client_disconnect_count},
        {"output_buffer_limit_count", worker.output_buffer_limit_count},
        {"redis_connected", worker.redis_connected},
        {"redis_contexts_reserved", worker.redis_contexts_reserved},
        {"mounts", std::move(mounts)},
        {"clients", std::move(clients)},
    };
}

} // namespace

std::string runtime_metrics_to_json(const RuntimeMetricsSnapshot &snapshot)
{
    const auto totals = totals_for(snapshot.workers);

    auto mounts = Json::array();
    auto clients = Json::array();
    auto workers = Json::array();
    for (const auto &worker : snapshot.workers) {
        for (const auto &mount : worker.mounts) {
            mounts.push_back(mount_to_json(mount));
        }
        for (const auto &client : worker.clients) {
            clients.push_back(client_to_json(client));
        }
        workers.push_back(worker_to_json(worker));
    }

    Json body{
        {"runtime_id", snapshot.runtime_id},
        {"running", snapshot.running},
        {"uptime_ms", snapshot.uptime_ms},
        {"worker_count", snapshot.worker_count},
        {"mount_count", snapshot.mount_count},
        {"connection_count", totals.active_sessions},
        {"server_count", totals.server_count},
        {"client_count", totals.client_count},
        {"bytes_in", totals.bytes_in},
        {"bytes_out", totals.bytes_out},
        {"fanout_write_count", totals.fanout_write_count},
        {"redis_publish_count", totals.redis_publish_count},
        {"redis_publish_bytes", totals.redis_publish_bytes},
        {"redis_publish_error_count", totals.redis_publish_error_count},
        {"redis_subscribe_message_count", totals.redis_subscribe_message_count},
        {"redis_subscribe_bytes", totals.redis_subscribe_bytes},
        {"redis_remote_fanout_write_count", totals.redis_remote_fanout_write_count},
        {"redis_remote_fanout_bytes", totals.redis_remote_fanout_bytes},
        {"redis_subscribed_mount_count", totals.redis_subscribed_mount_count},
        {"redis_position_report_count", totals.redis_position_report_count},
        {"redis_position_report_error_count", totals.redis_position_report_error_count},
        {"redis_error_count", totals.redis_error_count},
        {"slow_client_disconnect_count", totals.slow_client_disconnect_count},
        {"output_buffer_limit_count", totals.output_buffer_limit_count},
        {"mounts", std::move(mounts)},
        {"clients", std::move(clients)},
        {"mount_owners", mount_owners_to_json(snapshot.mount_owners)},
        {"workers", std::move(workers)},
    };
    return body.dump();
}

} // namespace navcaster::caster
