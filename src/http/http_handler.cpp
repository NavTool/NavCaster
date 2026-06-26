#include "http_handler.h"
#include "SysUsage.h"
#include "Caster_Core.h"
#include "account_controller.h"
#include "access_controller.h"
#include "access_repository.h"
#include "alias_controller.h"
#include "alias_repository.h"
#include "audit_log_service.h"
#include "blocking_redis_client.h"
#include "cluster_monitor_service.h"
#include "config_controller.h"
#include "config_repository.h"
#include "connection_history_service.h"
#include "controller_helpers.h"
#include "mountpoint_subscriber_service.h"
#include "node_log_level_service.h"
#include "node_history_service.h"
#include "operations_controller.h"
#include "redis_keys.h"
#include "redis_monitor_service.h"
#include "ring_log_service.h"
#include "ring_log_view.h"
#include "relay_controller.h"
#include "relay_repository.h"
#include "runtime_command_service.h"
#include "runtime_state_controller.h"
#include "runtime_state_repository.h"
#include "sourcetable_service.h"
#include "statistics_controller.h"
#include "status_service.h"
#include "source_controller.h"
#include "source_repository.h"
#include "sse_snapshot_service.h"
#include "system_event_service.h"
#include <spdlog/spdlog.h>
#include <ctime>

#define __class__ "http_handler"

namespace
{
    navcaster::storage::BlockingRedisClient &caster_redis_client()
    {
        static navcaster::storage::BlockingRedisClient client;
        return client;
    }

    navcaster::storage::BlockingRedisClient &auth_redis_client()
    {
        static navcaster::storage::BlockingRedisClient client;
        return client;
    }

    std::int64_t current_unix_seconds()
    {
        return static_cast<std::int64_t>(std::time(nullptr));
    }

} // anonymous namespace

http_handler::http_handler() {}
http_handler::~http_handler() {}

int http_handler::init(event_base *base, redis_adapter *caster_redis, redis_adapter *auth_redis, const HttpApiConfig &config)
{
    _caster_redis = caster_redis;
    _auth_redis = auth_redis;
    _config = config;

    // Initialize synchronous Redis connections for blocking API operations
    caster_redis_client().init(config.redis_host, config.redis_port, config.redis_password);
    auth_redis_client().init(config.auth_redis_host, config.auth_redis_port, config.auth_redis_password);

    // Ensure default access group exists
    {
        navcaster::storage::AccessRepository repo(caster_redis_client());
        repo.ensure_builtin_groups(current_unix_seconds());
        spdlog::info("[{}:{}]: Ensured default access group exists", __class__, __func__);
    }

    // Configure server
    _server.set_cors_origin(config.cors_origin);
    _server.add_public_path("/api/auth/login");
    _server.add_public_path("/api/status/health");
    _server.set_auth_validator([this](const std::string &token) -> bool
                               { return _auth_sessions.validate_token(token); });
    _server.set_actor_resolver([this](const std::string &token) -> std::string
                               { return _auth_sessions.lookup_user(token); });
    _server.set_audit_sink([this](const HttpRequest &req, const HttpResponse &resp,
                                  const std::string &actor, const std::string &client_ip)
                           { write_audit(req, resp, actor, client_ip); });

    // ==================== Auth ====================
    _server.route(EVHTTP_REQ_POST, "/api/auth/login", [this](auto &req, auto &resp)
                  { handle_login(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/auth/logout", [this](auto &req, auto &resp)
                  { handle_logout(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/auth/session", [this](auto &req, auto &resp)
                  { handle_v1_session(req, resp); });

    // ==================== V1 Operations Domain ====================
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/accounts", [this](auto &req, auto &resp)
                  { handle_v1_admin_accounts(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/v1/admin/accounts", [this](auto &req, auto &resp)
                  { handle_v1_admin_accounts(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/accounts/*", [this](auto &req, auto &resp)
                  { handle_v1_admin_account(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/v1/admin/accounts/*", [this](auto &req, auto &resp)
                  { handle_v1_admin_account(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/v1/admin/accounts/*", [this](auto &req, auto &resp)
                  { handle_v1_admin_account(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/v1/admin/accounts/*", [this](auto &req, auto &resp)
                  { handle_v1_admin_account(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/mount-point-groups", [this](auto &req, auto &resp)
                  { handle_v1_admin_mount_point_groups(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/v1/admin/mount-point-groups", [this](auto &req, auto &resp)
                  { handle_v1_admin_mount_point_groups(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/v1/admin/mount-point-groups/*", [this](auto &req, auto &resp)
                  { handle_v1_admin_mount_point_group_members(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/mount-points", [this](auto &req, auto &resp)
                  { handle_v1_admin_mount_points(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/v1/admin/mount-points/*", [this](auto &req, auto &resp)
                  { handle_v1_admin_mount_points(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/access-accounts", [this](auto &req, auto &resp)
                  { handle_v1_admin_access_accounts(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/subscriptions", [this](auto &req, auto &resp)
                  { handle_v1_admin_subscriptions(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/v1/admin/subscriptions", [this](auto &req, auto &resp)
                  { handle_v1_admin_subscriptions(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/stations", [this](auto &req, auto &resp)
                  { handle_v1_admin_stations(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/usage", [this](auto &req, auto &resp)
                  { handle_v1_admin_usage(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/v1/admin/supply-usage", [this](auto &req, auto &resp)
                  { handle_v1_admin_supply_usage(req, resp); });

    // ==================== Accounts ====================
    _server.route(EVHTTP_REQ_GET, "/api/accounts", [this](auto &req, auto &resp)
                  { handle_get_accounts(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/accounts/*", [this](auto &req, auto &resp)
                  { handle_get_account(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/accounts", [this](auto &req, auto &resp)
                  { handle_create_account(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/accounts/*", [this](auto &req, auto &resp)
                  { handle_update_account(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/accounts/*", [this](auto &req, auto &resp)
                  { handle_delete_account(req, resp); });

    // ==================== Account Actives ====================
    _server.route(EVHTTP_REQ_GET, "/api/accounts/active", [this](auto &req, auto &resp)
                  { handle_get_account_actives(req, resp); });

    // ==================== Sources ====================
    _server.route(EVHTTP_REQ_GET, "/api/sources", [this](auto &req, auto &resp)
                  { handle_get_sources(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/sources/*", [this](auto &req, auto &resp)
                  { handle_get_source(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/sources", [this](auto &req, auto &resp)
                  { handle_create_source(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/sources/*", [this](auto &req, auto &resp)
                  { handle_update_source(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/sources/*", [this](auto &req, auto &resp)
                  { handle_delete_source(req, resp); });

    // ==================== Servers (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/servers", [this](auto &req, auto &resp)
                  { handle_get_servers(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/servers/kick/*", [this](auto &req, auto &resp)
                  { handle_kick_server(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/servers/*", [this](auto &req, auto &resp)
                  { handle_get_server(req, resp); });

    // ==================== Clients (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/clients", [this](auto &req, auto &resp)
                  { handle_get_clients(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/clients/kick/*", [this](auto &req, auto &resp)
                  { handle_kick_client(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/clients/*", [this](auto &req, auto &resp)
                  { handle_get_client(req, resp); });

    // ==================== Streams (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/streams", [this](auto &req, auto &resp)
                  { handle_get_streams(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/streams/*", [this](auto &req, auto &resp)
                  { handle_get_stream(req, resp); });

    // ==================== Aliases ====================
    _server.route(EVHTTP_REQ_GET, "/api/aliases", [this](auto &req, auto &resp)
                  { handle_get_aliases(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/aliases/*", [this](auto &req, auto &resp)
                  { handle_get_alias(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/aliases", [this](auto &req, auto &resp)
                  { handle_create_alias(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/aliases/*", [this](auto &req, auto &resp)
                  { handle_update_alias(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/aliases/*", [this](auto &req, auto &resp)
                  { handle_delete_alias(req, resp); });

    // ==================== Access Groups ====================
    _server.route(EVHTTP_REQ_GET, "/api/access/groups", [this](auto &req, auto &resp)
                  { handle_get_access_groups(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/access/groups/*", [this](auto &req, auto &resp)
                  { handle_get_access_group(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/access/groups", [this](auto &req, auto &resp)
                  { handle_create_access_group(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/access/groups/*", [this](auto &req, auto &resp)
                  { handle_update_access_group(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/access/groups/*", [this](auto &req, auto &resp)
                  { handle_delete_access_group(req, resp); });

    // ==================== Access Items ====================
    _server.route(EVHTTP_REQ_GET, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_get_access_items(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_create_access_item(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_update_access_item(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/access/items/*", [this](auto &req, auto &resp)
                  { handle_delete_access_item(req, resp); });

    // ==================== Pull Relays ====================
    _server.route(EVHTTP_REQ_GET, "/api/relays/pull", [this](auto &req, auto &resp)
                  { handle_get_pulls(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/pull/status", [this](auto &req, auto &resp)
                  { handle_get_pull_states(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/pull/*", [this](auto &req, auto &resp)
                  { handle_get_pull(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/pull", [this](auto &req, auto &resp)
                  { handle_create_pull(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/relays/pull/*", [this](auto &req, auto &resp)
                  { handle_update_pull(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/relays/pull/*", [this](auto &req, auto &resp)
                  { handle_delete_pull(req, resp); });

    // ==================== Push Relays ====================
    _server.route(EVHTTP_REQ_GET, "/api/relays/push", [this](auto &req, auto &resp)
                  { handle_get_pushs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/push/status", [this](auto &req, auto &resp)
                  { handle_get_push_states(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/relays/push/*", [this](auto &req, auto &resp)
                  { handle_get_push(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/push", [this](auto &req, auto &resp)
                  { handle_create_push(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/relays/push/*", [this](auto &req, auto &resp)
                  { handle_update_push(req, resp); });
    _server.route(EVHTTP_REQ_DELETE, "/api/relays/push/*", [this](auto &req, auto &resp)
                  { handle_delete_push(req, resp); });

    // ==================== Relay Start/Stop ====================
    _server.route(EVHTTP_REQ_POST, "/api/relays/pull/start/*", [this](auto &req, auto &resp)
                  { handle_relay_start(req, resp, navcaster::storage::RelayKind::Pull); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/pull/stop/*", [this](auto &req, auto &resp)
                  { handle_relay_stop(req, resp, navcaster::storage::RelayKind::Pull); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/push/start/*", [this](auto &req, auto &resp)
                  { handle_relay_start(req, resp, navcaster::storage::RelayKind::Push); });
    _server.route(EVHTTP_REQ_POST, "/api/relays/push/stop/*", [this](auto &req, auto &resp)
                  { handle_relay_stop(req, resp, navcaster::storage::RelayKind::Push); });

    // ==================== Nodes (read-only) ====================
    _server.route(EVHTTP_REQ_GET, "/api/nodes", [this](auto &req, auto &resp)
                  { handle_get_nodes(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/nodes/history/*", [this](auto &req, auto &resp)
                  { handle_get_node_history(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/nodes/*", [this](auto &req, auto &resp)
                  { handle_get_node(req, resp); });

    // ==================== Mountpoints ====================
    _server.route(EVHTTP_REQ_GET, "/api/mountpoints/subscribers", [this](auto &req, auto &resp)
                  { handle_get_mountpoint_subscribers(req, resp); });

    // ==================== Status ====================
    _server.route(EVHTTP_REQ_GET, "/api/status", [this](auto &req, auto &resp)
                  { handle_get_status(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/status/health", [this](auto &req, auto &resp)
                  { handle_get_health(req, resp); });

    // ==================== Connection History ====================
    _server.route(EVHTTP_REQ_GET, "/api/logs/servers", [this](auto &req, auto &resp)
                  { handle_get_server_logs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/logs/clients", [this](auto &req, auto &resp)
                  { handle_get_client_logs(req, resp); });

    // ==================== Statistics ====================
    _server.route(EVHTTP_REQ_GET, "/api/stats/overview", [this](auto &req, auto &resp)
                  { handle_get_stats_overview(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/daily/*", [this](auto &req, auto &resp)
                  { handle_get_stats_daily(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/mountpoints/ranking", [this](auto &req, auto &resp)
                  { handle_get_stats_mpt_ranking(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/mountpoints/history/*", [this](auto &req, auto &resp)
                  { handle_get_stats_mpt_history(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/users/ranking", [this](auto &req, auto &resp)
                  { handle_get_stats_usr_ranking(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/stats/users/history/*", [this](auto &req, auto &resp)
                  { handle_get_stats_usr_history(req, resp); });

    // ==================== Configuration ====================
    _server.route(EVHTTP_REQ_GET, "/api/config", [this](auto &req, auto &resp)
                  { handle_get_configs(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/config/*", [this](auto &req, auto &resp)
                  { handle_get_config(req, resp); });
    _server.route(EVHTTP_REQ_PUT, "/api/config/*", [this](auto &req, auto &resp)
                  { handle_update_config(req, resp); });

    // ==================== Monitoring ====================
    _server.route(EVHTTP_REQ_GET, "/api/monitor/redis", [this](auto &req, auto &resp)
                  { handle_get_monitor_redis(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/monitor/redis/keys", [this](auto &req, auto &resp)
                  { handle_get_monitor_redis_keys(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/monitor/redis/history", [this](auto &req, auto &resp)
                  { handle_get_monitor_redis_history(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/monitor/cluster", [this](auto &req, auto &resp)
                  { handle_get_monitor_cluster(req, resp); });

    // ==================== V3 运维接口 ====================
    _server.route(EVHTTP_REQ_GET, "/api/audit", [this](auto &req, auto &resp)
                  { handle_get_audit(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/logs/ring", [this](auto &req, auto &resp)
                  { handle_get_logs_ring(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/system/events", [this](auto &req, auto &resp)
                  { handle_get_system_events(req, resp); });
    _server.route(EVHTTP_REQ_POST, "/api/nodes/log-level/*", [this](auto &req, auto &resp)
                  { handle_set_node_log_level(req, resp); });

    // ==================== Utilities ====================
    _server.route(EVHTTP_REQ_POST, "/api/utils/sourcetable", [this](auto &req, auto &resp)
                  { handle_fetch_sourcetable(req, resp); });
    _server.route(EVHTTP_REQ_GET, "/api/utils/sourcetable/local", [this](auto &req, auto &resp)
                  { handle_local_sourcetable(req, resp); });

    // ==================== Static File Serving ====================
    if (!config.web_root.empty())
    {
        _server.set_static_root(config.web_root);
    }

    // ==================== SSE (Server-Sent Events) ====================
    _server.route_raw(EVHTTP_REQ_GET, "/api/events/stream",
                      [this](evhttp_request *raw_req, const HttpRequest &req)
                      { handle_sse_stream(raw_req, req); });
    _server.add_public_path("/api/events/stream"); // Auth via query param token

    // Start server
    int ret = _server.init(base, config.port, config.bind_addr);
    if (ret != 0)
    {
        spdlog::error("[{}:{}]: Failed to start HTTP API server", __class__, __func__);
        return ret;
    }

    // Initialize SSE manager with data channels
    _sse.init(base, 2); // 2-second update interval
    _sse.set_max_clients(200); // V3 C3: 限制 SSE 并发连接

    // V3 B4: Redis 状态采样定时器 (60s 周期，保留 24h = 1440 点)
    _redis_sample_timer = event_new(base, -1, EV_PERSIST, on_redis_sample_timer, this);
    if (_redis_sample_timer)
    {
        struct timeval tv {60, 0};
        event_add(_redis_sample_timer, &tv);
        spdlog::info("[{}:{}]: Redis history sampling enabled (60s interval)", __class__, __func__);
    }

    // Register SSE channels through repository-backed snapshot service.
    static navcaster::http_api::SseSnapshotService sse_snapshots(
        caster_redis_client(),
        auth_redis_client());
    sse_snapshots.register_channels(_sse);

    spdlog::info("[{}:{}]: HTTP API handler initialized on port {}", __class__, __func__, config.port);
    return 0;
}

// ==================== Helpers ====================

std::string http_handler::get_resource_id(const HttpRequest &req) const
{
    // Return last path segment
    if (!req.path_segments.empty())
        return req.path_segments.back();
    return {};
}

std::string http_handler::get_path_segment(const HttpRequest &req, size_t index) const
{
    if (index < req.path_segments.size())
        return req.path_segments[index];
    return {};
}

// ==================== Auth ====================

void http_handler::handle_login(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::storage::ConfigRepository config_repo(caster_redis_client());
    auto result = _auth_sessions.login(
        req.body,
        {_config.admin_user, _config.admin_password},
        config_repo.get_config(navcaster::storage::ConfigSection::Auth));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_logout(const HttpRequest &req, HttpResponse &resp)
{
    auto it = req.headers.find("Authorization");
    auto result = _auth_sessions.logout(it != req.headers.end() ? it->second : std::string());
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_session(const HttpRequest &req, HttpResponse &resp)
{
    auto it = req.headers.find("Authorization");
    const std::string token = navcaster::http_api::bearer_token_from_authorization(it != req.headers.end() ? it->second : std::string());
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.session_subject(_auth_sessions.lookup_user(token));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_accounts(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = req.method == EVHTTP_REQ_POST ? controller.create_account(req.body) : controller.list_accounts();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_account(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    const std::string account_id = get_path_segment(req, 4);
    navcaster::http_api::ControllerResponse result;
    if (req.path_segments.size() >= 6 && get_path_segment(req, 5) == "group-grants" && req.method == EVHTTP_REQ_PUT)
    {
        result = controller.grant_account_group(account_id, req.body);
    }
    else if (req.path_segments.size() >= 6 && get_path_segment(req, 5) == "balance-adjustments" && req.method == EVHTTP_REQ_POST)
    {
        result = controller.append_balance_adjustment(account_id, req.body);
    }
    else if (req.path_segments.size() > 5)
    {
        result.status_code = 404;
        result.body = R"({"error":"Not Found"})";
    }
    else if (req.method == EVHTTP_REQ_PUT)
    {
        result = controller.update_account(account_id, req.body);
    }
    else if (req.method == EVHTTP_REQ_DELETE)
    {
        result = controller.delete_account(account_id);
    }
    else
    {
        result = controller.get_account(account_id);
    }
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_account_group_grants(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.grant_account_group(get_path_segment(req, 4), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_account_balance_adjustments(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.append_balance_adjustment(get_path_segment(req, 4), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_mount_point_groups(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = req.method == EVHTTP_REQ_POST ? controller.create_mount_point_group(req.body) : controller.list_mount_point_groups();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_mount_point_group_members(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    navcaster::http_api::ControllerResponse result;
    if (req.path_segments.size() == 6 && get_path_segment(req, 5) == "members")
    {
        result = controller.add_mount_point_group_member(get_path_segment(req, 4), req.body);
    }
    else
    {
        result.status_code = 404;
        result.body = R"({"error":"Not Found"})";
    }
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_mount_points(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    if (req.method == EVHTTP_REQ_PUT)
    {
        auto result = controller.create_mount_point(get_resource_id(req), req.body);
        resp.status_code = result.status_code;
        resp.body = std::move(result.body);
        return;
    }
    auto result = controller.list_mount_points();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_access_accounts(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.list_access_accounts();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_subscriptions(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = req.method == EVHTTP_REQ_POST ? controller.create_subscription(req.body) : controller.list_subscriptions();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_stations(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.list_stations();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_usage(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto period = req.query_params.find("period");
    auto result = controller.list_usage(period == req.query_params.end() ? std::string{} : period->second);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_v1_admin_supply_usage(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::OperationsController controller(auth_redis_client(), current_unix_seconds());
    auto period = req.query_params.find("period");
    auto result = controller.list_supply_usage(period == req.query_params.end() ? std::string{} : period->second);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Accounts (ACT:RECORD) — uses auth redis ====================

void http_handler::handle_get_accounts(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccountController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.list_accounts();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_account(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccountController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.get_account(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_account(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccountController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.create_account(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_account(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccountController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.update_account(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_account(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccountController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.delete_account(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_account_actives(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccountController controller(auth_redis_client(), current_unix_seconds());
    auto result = controller.list_active_sessions();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Sources (MPT:RECORD) ====================

void http_handler::handle_get_sources(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.list_sources();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.get_source(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.create_source(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.update_source(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_source(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourceController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.delete_source(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Servers (MPT:STAT) read-only ====================

void http_handler::handle_get_servers(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.list(navcaster::storage::RuntimeStateKind::Server);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_server(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.get(navcaster::storage::RuntimeStateKind::Server, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Clients (USR:STAT) read-only ====================

void http_handler::handle_get_clients(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.list(navcaster::storage::RuntimeStateKind::Client);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_client(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.get(navcaster::storage::RuntimeStateKind::Client, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Force Offline (Kick) ====================

void http_handler::handle_kick_server(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeCommandService service(caster_redis_client());
    auto result = service.kick(navcaster::storage::RuntimeStateKind::Server, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_kick_client(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeCommandService service(caster_redis_client());
    auto result = service.kick(navcaster::storage::RuntimeStateKind::Client, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Streams (STR:STAT) read-only ====================

void http_handler::handle_get_streams(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.list(navcaster::storage::RuntimeStateKind::Stream);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_stream(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.get(navcaster::storage::RuntimeStateKind::Stream, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Aliases (ALIAS:RULE) ====================

void http_handler::handle_get_aliases(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.list_aliases();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.get_alias(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.create_alias(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.update_alias(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_alias(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AliasController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.delete_alias(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Access Groups (ACCESS:GROUP) ====================

void http_handler::handle_get_access_groups(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.list_groups();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.get_group(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.create_group(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.update_group(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_access_group(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.delete_group(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Access Items (ACCESS:ITEM:<group_uid>) ====================

void http_handler::handle_get_access_items(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.list_items(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_access_item(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.create_item(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_access_item(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.update_item(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_access_item(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::AccessController controller(caster_redis_client(), current_unix_seconds());
    auto result = controller.delete_item(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Pull Relays (PULL:RECORD / PULL:STAT) ====================

void http_handler::handle_get_pulls(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.list_records(navcaster::storage::RelayKind::Pull);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_pull(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.get_record(navcaster::storage::RelayKind::Pull, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_pull(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.create_record(navcaster::storage::RelayKind::Pull, req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_pull(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.update_record(navcaster::storage::RelayKind::Pull, get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_pull(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.delete_record(navcaster::storage::RelayKind::Pull, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_pull_states(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.list_states(navcaster::storage::RelayKind::Pull);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Push Relays (PUSH:RECORD / PUSH:STAT) ====================

void http_handler::handle_get_pushs(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.list_records(navcaster::storage::RelayKind::Push);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_push(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.get_record(navcaster::storage::RelayKind::Push, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_create_push(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.create_record(navcaster::storage::RelayKind::Push, req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_push(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.update_record(navcaster::storage::RelayKind::Push, get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_delete_push(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.delete_record(navcaster::storage::RelayKind::Push, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_push_states(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.list_states(navcaster::storage::RelayKind::Push);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Relay Start/Stop ====================

void http_handler::handle_relay_start(const HttpRequest &req, HttpResponse &resp, navcaster::storage::RelayKind kind)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.set_enabled(kind, get_resource_id(req), true);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_relay_stop(const HttpRequest &req, HttpResponse &resp, navcaster::storage::RelayKind kind)
{
    navcaster::http_api::RelayController controller(caster_redis_client());
    auto result = controller.set_enabled(kind, get_resource_id(req), false);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Nodes (CASTER:NODE) read-only ====================

void http_handler::handle_get_nodes(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.list(navcaster::storage::RuntimeStateKind::Node);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_node(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::RuntimeStateController controller(caster_redis_client());
    auto result = controller.get(navcaster::storage::RuntimeStateKind::Node, get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Node History (NODE:HISTORY:*) read-only ====================

void http_handler::handle_get_node_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string node_id = get_resource_id(req);

    // range 参数决定读取哪个层级: raw(默认) / 1m / 5m
    std::string range = "raw";
    auto range_it = req.query_params.find("range");
    if (range_it != req.query_params.end())
    {
        range = range_it->second;
    }

    // limit 参数
    long long limit = 17280;
    auto it = req.query_params.find("limit");
    if (it != req.query_params.end())
    {
        try { limit = std::stoll(it->second); } catch (...) {}
        if (limit <= 0) limit = 17280;
    }

    navcaster::http_api::NodeHistoryService service(caster_redis_client());
    auto result = service.list(node_id, range, limit);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Connection History (LOG:MPT / LOG:USR) read-only ====================
// LOG:MPT 与 LOG:USR 是 hash key 的前缀，实际数据在 LOG:MPT:<mount> / LOG:USR:<user>
// 这里需要 SCAN+HGETALL 聚合，而不是直接 HGETALL

void http_handler::handle_get_server_logs(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::ConnectionHistoryService service(caster_redis_client());
    auto result = service.list(navcaster::storage::ConnectionHistoryKind::Server);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_client_logs(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::ConnectionHistoryService service(caster_redis_client());
    auto result = service.list(navcaster::storage::ConnectionHistoryKind::Client);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_stats_overview(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::StatisticsController controller(
        caster_redis_client(),
        static_cast<long long>(time(nullptr)));
    auto result = controller.overview(req.query_params);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_stats_daily(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::StatisticsController controller(
        caster_redis_client(),
        static_cast<long long>(time(nullptr)));
    auto result = controller.daily(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_stats_mpt_ranking(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::StatisticsController controller(
        caster_redis_client(),
        static_cast<long long>(time(nullptr)));
    auto result = controller.mountpoint_ranking(req.query_params);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_stats_usr_ranking(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::StatisticsController controller(
        caster_redis_client(),
        static_cast<long long>(time(nullptr)));
    auto result = controller.user_ranking(req.query_params);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// GET /api/stats/mountpoints/history/{mount} — connection history for a specific mountpoint
void http_handler::handle_get_stats_mpt_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string mount = get_resource_id(req);
    navcaster::http_api::ConnectionHistoryService service(caster_redis_client());
    auto result = service.detail(
        navcaster::storage::ConnectionHistoryKind::Server,
        mount,
        static_cast<long long>(time(nullptr)));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// GET /api/stats/users/history/{user} — connection history for a specific user
void http_handler::handle_get_stats_usr_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string user = get_resource_id(req);
    navcaster::http_api::ConnectionHistoryService service(caster_redis_client());
    auto result = service.detail(
        navcaster::storage::ConnectionHistoryKind::Client,
        user,
        static_cast<long long>(time(nullptr)));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Mountpoint Subscribers ====================

void http_handler::handle_get_mountpoint_subscribers(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::MountpointSubscriberService service(caster_redis_client());
    auto result = service.list();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Status ====================

void http_handler::handle_get_status(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    auto &redis = caster_redis_client();
    navcaster::http_api::StatusSnapshot snapshot;
    snapshot.cpu_percent = SysUsage::getInstance()->getProcessCPU();
    snapshot.memory_bytes = SysUsage::getInstance()->getProcessMemory();
    snapshot.caster_status = CASTER::Get_Status();
    snapshot.redis_caster_connected = _caster_redis ? _caster_redis->is_connected() : false;
    snapshot.redis_auth_connected = _auth_redis ? _auth_redis->is_connected() : false;
    snapshot.ntrip_port = _config.ntrip_port;
    snapshot.master_node = redis.get(navcaster::redis_keys::CASTER_MASTER);
    snapshot.sse_clients = static_cast<unsigned long long>(_sse.client_count());
    snapshot.sse_max_clients = static_cast<unsigned long long>(_sse.max_clients());
    snapshot.node_id = CASTER::Get_Node_ID();
    snapshot.log_level = spdlog::level::to_string_view(spdlog::get_level()).data();

    navcaster::http_api::StatusService service;
    auto result = service.status(snapshot);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_health(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::StatusService service;
    auto result = service.health();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Utility: Fetch Remote Sourcetable ====================

void http_handler::handle_fetch_sourcetable(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::SourcetableService service;
    auto result = service.fetch_remote(req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_local_sourcetable(const HttpRequest &req, HttpResponse &resp)
{
    // 直接从 CasterCore 获取本地源表，不通过 NTRIP 协议（避免同线程阻塞死锁）
    std::string source_table_text = CASTER::Get_Source_Table_Text();
    navcaster::http_api::SourcetableService service;
    auto result = service.local_from_text(source_table_text);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== Configuration ====================

void http_handler::save_config(const std::string &section, const std::string &json_str)
{
    navcaster::http_api::ConfigController controller(
        caster_redis_client(),
        {_config.admin_user, _config.admin_password});
    controller.save_config(section, json_str);
}

void http_handler::handle_get_configs(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::ConfigController controller(
        caster_redis_client(),
        {_config.admin_user, _config.admin_password});
    auto result = controller.get_configs();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_config(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::ConfigController controller(
        caster_redis_client(),
        {_config.admin_user, _config.admin_password});
    auto result = controller.get_config(get_resource_id(req));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_update_config(const HttpRequest &req, HttpResponse &resp)
{
    navcaster::http_api::ConfigController controller(
        caster_redis_client(),
        {_config.admin_user, _config.admin_password});
    auto result = controller.update_config(get_resource_id(req), req.body);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== SSE ====================

void http_handler::handle_sse_stream(evhttp_request *raw_req, const HttpRequest &req)
{
    // Auth via query parameter: ?token=xxx
    auto it = req.query_params.find("token");
    if (it == req.query_params.end() || !_auth_sessions.validate_token(it->second))
    {
        // Also check Authorization header (already parsed in req.headers)
        auto auth_it = req.headers.find("Authorization");
        bool authed = false;
        if (auth_it != req.headers.end() && auth_it->second.size() > 7)
        {
            std::string token = auth_it->second.substr(7);
            if (_auth_sessions.validate_token(token))
                authed = true;
        }
        if (!authed)
        {
            evhttp_send_error(raw_req, 401, "Unauthorized");
            return;
        }
    }

    // Get optional channel filter from query params
    auto ch_it = req.query_params.find("channels");
    std::string channels = (ch_it != req.query_params.end()) ? ch_it->second : "*";

    _sse.add_client(raw_req, channels);
}

// ==================== Monitoring Handlers ====================

void http_handler::handle_get_monitor_redis(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::RedisMonitorService service(caster_redis_client());
    auto result = service.summary();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_monitor_redis_keys(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::RedisMonitorService service(caster_redis_client());
    auto result = service.keys();
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_monitor_cluster(const HttpRequest &req, HttpResponse &resp)
{
    (void)req;
    navcaster::http_api::ClusterMonitorService service(caster_redis_client());
    auto result = service.snapshot(static_cast<long long>(std::time(nullptr)));
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

// ==================== V3 \u5ba1\u8ba1 / \u73af\u5f62\u65e5\u5fd7 / Redis \u91c7\u6837 / \u8282\u70b9\u4e8b\u4ef6 / \u52a8\u6001\u65e5\u5fd7\u7ea7\u522b ====================

void http_handler::write_audit(const HttpRequest &req, const HttpResponse &resp,
                               const std::string &actor, const std::string &client_ip)
{
    navcaster::http_api::AuditLogService service(caster_redis_client());
    service.write(req, resp, actor, client_ip, CASTER::Get_Node_ID(), std::time(nullptr));
}

void http_handler::handle_get_audit(const HttpRequest &req, HttpResponse &resp)
{
    long long limit = 100;
    long long cursor = 0;
    std::string filter_actor, filter_action, filter_target;
    auto it_l = req.query_params.find("limit");
    if (it_l != req.query_params.end()) try { limit = std::stoll(it_l->second); } catch (...) {}
    auto it_c = req.query_params.find("cursor");
    if (it_c != req.query_params.end()) try { cursor = std::stoll(it_c->second); } catch (...) {}
    auto it_a = req.query_params.find("actor");  if (it_a != req.query_params.end()) filter_actor = it_a->second;
    auto it_x = req.query_params.find("action"); if (it_x != req.query_params.end()) filter_action = it_x->second;
    auto it_t = req.query_params.find("target"); if (it_t != req.query_params.end()) filter_target = it_t->second;

    navcaster::http_api::AuditLogService service(caster_redis_client());
    auto result = service.list(limit, cursor, filter_actor, filter_action, filter_target);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_logs_ring(const HttpRequest &req, HttpResponse &resp)
{
    size_t n = 500;
    auto it_n = req.query_params.find("n");
    if (it_n != req.query_params.end()) try { n = std::stoul(it_n->second); } catch (...) {}

    std::string level_str;
    auto it_l = req.query_params.find("level");
    if (it_l != req.query_params.end()) level_str = it_l->second;

    navcaster::http_api::RingLogService service(
        [](std::size_t count) { return ring_log_view::last_n(count); });
    auto result = service.list(n, level_str, static_cast<long long>(std::time(nullptr)) * 1000LL);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_get_system_events(const HttpRequest &req, HttpResponse &resp)
{
    long long limit = 100;
    auto it_l = req.query_params.find("limit");
    if (it_l != req.query_params.end()) try { limit = std::stoll(it_l->second); } catch (...) {}

    navcaster::http_api::SystemEventService service(caster_redis_client());
    auto result = service.list(limit);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}

void http_handler::handle_set_node_log_level(const HttpRequest &req, HttpResponse &resp)
{
    std::string id = get_resource_id(req);
    std::string my_id = CASTER::Get_Node_ID();
    navcaster::http_api::NodeLogLevelService service;
    auto result = service.set_level(id, my_id, req.body);
    if (result.should_apply)
    {
        spdlog::set_level(result.level);
        spdlog::warn("[http]: log level changed to {} (by remote)", result.level_text);
    }
    resp.status_code = result.response.status_code;
    resp.body = std::move(result.response.body);
}

void http_handler::on_redis_sample_timer(evutil_socket_t /*fd*/, short /*what*/, void *arg)
{
    static_cast<http_handler *>(arg)->sample_redis_history();
}

void http_handler::sample_redis_history()
{
    navcaster::http_api::RedisMonitorService service(caster_redis_client());
    if (!service.sample_history(static_cast<long long>(std::time(nullptr))))
    {
        spdlog::warn("[{}:{}]: skip redis history sample, INFO missing required sections or used_memory is 0", __class__, __func__);
    }
}

void http_handler::handle_get_monitor_redis_history(const HttpRequest &req, HttpResponse &resp)
{
    std::string range;
    auto it = req.query_params.find("range");
    if (it != req.query_params.end())
    {
        range = it->second;
    }

    navcaster::http_api::RedisMonitorService service(caster_redis_client());
    auto result = service.history(range);
    resp.status_code = result.status_code;
    resp.body = std::move(result.body);
}
