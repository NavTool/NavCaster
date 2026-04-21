#pragma once

#include "http_server.h"
#include "redis_adapter.h"
#include "sse_manager.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <random>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

struct HttpApiConfig
{
    int port = 8080;
    std::string bind_addr = "0.0.0.0";
    std::string cors_origin = "*";
    std::string admin_user = "admin";
    std::string admin_password = "admin";
    std::string web_root; // path to static web files (empty = disabled)
    int ntrip_port = 2101; // NTRIP caster listen port (for source table fetch)

    // Redis connection for caster data (sync blocking)
    std::string redis_host = "127.0.0.1";
    int redis_port = 6379;
    std::string redis_password;

    // Redis connection for auth data (accounts etc.)
    std::string auth_redis_host = "127.0.0.1";
    int auth_redis_port = 6379;
    std::string auth_redis_password;

    // 强制开启 HTTP API (默认仅主节点开启)
    bool force_enable = false;
};

class http_handler
{
public:
    http_handler();
    ~http_handler();

    // Initialize and register all routes
    int init(event_base *base, redis_adapter *caster_redis, redis_adapter *auth_redis, const HttpApiConfig &config);

    // Save configuration JSON to Redis
    void save_config(const std::string &section, const std::string &json_str);

private:
    // Auth endpoints
    void handle_login(const HttpRequest &req, HttpResponse &resp);
    void handle_logout(const HttpRequest &req, HttpResponse &resp);

    // Generic HASH CRUD helpers (used by all resource handlers)
    // These respond asynchronously via evhttp deferred response mechanism.
    // But since evhttp callbacks run on the same event loop as Redis callbacks,
    // we can use a synchronous-looking pattern within the same thread.

    // For the sync-in-event-loop pattern, we use a response holder
    struct DeferredResponse
    {
        HttpResponse *resp;
        bool done = false;
    };

    // Account endpoints (ACT:RECORD) — uses auth_redis
    void handle_get_accounts(const HttpRequest &req, HttpResponse &resp);
    void handle_get_account(const HttpRequest &req, HttpResponse &resp);
    void handle_create_account(const HttpRequest &req, HttpResponse &resp);
    void handle_update_account(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_account(const HttpRequest &req, HttpResponse &resp);

    // Account Active (STR:ACTIVE) — uses auth_redis, read-only
    void handle_get_account_actives(const HttpRequest &req, HttpResponse &resp);

    // Source Records (MPT:RECORD)
    void handle_get_sources(const HttpRequest &req, HttpResponse &resp);
    void handle_get_source(const HttpRequest &req, HttpResponse &resp);
    void handle_create_source(const HttpRequest &req, HttpResponse &resp);
    void handle_update_source(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_source(const HttpRequest &req, HttpResponse &resp);

    // Server States (MPT:STAT) — read-only
    void handle_get_servers(const HttpRequest &req, HttpResponse &resp);
    void handle_get_server(const HttpRequest &req, HttpResponse &resp);

    // Client States (USR:STAT) — read-only
    void handle_get_clients(const HttpRequest &req, HttpResponse &resp);
    void handle_get_client(const HttpRequest &req, HttpResponse &resp);

    // Stream States (STR:STAT) — read-only
    void handle_get_streams(const HttpRequest &req, HttpResponse &resp);
    void handle_get_stream(const HttpRequest &req, HttpResponse &resp);

    // Alias Rules (ALIAS:RULE)
    void handle_get_aliases(const HttpRequest &req, HttpResponse &resp);
    void handle_get_alias(const HttpRequest &req, HttpResponse &resp);
    void handle_create_alias(const HttpRequest &req, HttpResponse &resp);
    void handle_update_alias(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_alias(const HttpRequest &req, HttpResponse &resp);

    // Access Groups (ACCESS:GROUP)
    void handle_get_access_groups(const HttpRequest &req, HttpResponse &resp);
    void handle_get_access_group(const HttpRequest &req, HttpResponse &resp);
    void handle_create_access_group(const HttpRequest &req, HttpResponse &resp);
    void handle_update_access_group(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_access_group(const HttpRequest &req, HttpResponse &resp);

    // Access Items (ACCESS:ITEM:<group_uid>)
    void handle_get_access_items(const HttpRequest &req, HttpResponse &resp);
    void handle_create_access_item(const HttpRequest &req, HttpResponse &resp);
    void handle_update_access_item(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_access_item(const HttpRequest &req, HttpResponse &resp);

    // Pull Relays (PULL:RECORD / PULL:STAT)
    void handle_get_pulls(const HttpRequest &req, HttpResponse &resp);
    void handle_get_pull(const HttpRequest &req, HttpResponse &resp);
    void handle_create_pull(const HttpRequest &req, HttpResponse &resp);
    void handle_update_pull(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_pull(const HttpRequest &req, HttpResponse &resp);
    void handle_get_pull_states(const HttpRequest &req, HttpResponse &resp);

    // Push Relays (PUSH:RECORD / PUSH:STAT)
    void handle_get_pushs(const HttpRequest &req, HttpResponse &resp);
    void handle_get_push(const HttpRequest &req, HttpResponse &resp);
    void handle_create_push(const HttpRequest &req, HttpResponse &resp);
    void handle_update_push(const HttpRequest &req, HttpResponse &resp);
    void handle_delete_push(const HttpRequest &req, HttpResponse &resp);
    void handle_get_push_states(const HttpRequest &req, HttpResponse &resp);

    // Relay start/stop
    void handle_relay_start(const HttpRequest &req, HttpResponse &resp);
    void handle_relay_stop(const HttpRequest &req, HttpResponse &resp);

    // Cluster Nodes (CASTER:NODE) — read-only
    void handle_get_nodes(const HttpRequest &req, HttpResponse &resp);
    void handle_get_node(const HttpRequest &req, HttpResponse &resp);

    // Mountpoint subscribers
    void handle_get_mountpoint_subscribers(const HttpRequest &req, HttpResponse &resp);

    // System status
    void handle_get_status(const HttpRequest &req, HttpResponse &resp);
    void handle_get_health(const HttpRequest &req, HttpResponse &resp);

    // Connection history (LOG:MPT / LOG:USR)
    void handle_get_server_logs(const HttpRequest &req, HttpResponse &resp);
    void handle_get_client_logs(const HttpRequest &req, HttpResponse &resp);

    // Node history (NODE:HISTORY:*)
    void handle_get_node_history(const HttpRequest &req, HttpResponse &resp);

    // Statistics (based on LOG:MPT / LOG:USR)
    void handle_get_stats_overview(const HttpRequest &req, HttpResponse &resp);
    void handle_get_stats_daily(const HttpRequest &req, HttpResponse &resp);
    void handle_get_stats_mpt_ranking(const HttpRequest &req, HttpResponse &resp);
    void handle_get_stats_usr_ranking(const HttpRequest &req, HttpResponse &resp);
    void handle_get_stats_mpt_history(const HttpRequest &req, HttpResponse &resp);
    void handle_get_stats_usr_history(const HttpRequest &req, HttpResponse &resp);

    // Configuration (CONF:*)
    void handle_get_configs(const HttpRequest &req, HttpResponse &resp);
    void handle_get_config(const HttpRequest &req, HttpResponse &resp);
    void handle_update_config(const HttpRequest &req, HttpResponse &resp);

    // Monitoring (Redis + Cluster)
    void handle_get_monitor_redis(const HttpRequest &req, HttpResponse &resp);
    void handle_get_monitor_redis_keys(const HttpRequest &req, HttpResponse &resp);
    void handle_get_monitor_cluster(const HttpRequest &req, HttpResponse &resp);

    // Utility endpoints
    void handle_fetch_sourcetable(const HttpRequest &req, HttpResponse &resp);
    void handle_local_sourcetable(const HttpRequest &req, HttpResponse &resp);

    // SSE endpoint
    void handle_sse_stream(evhttp_request *raw_req, const HttpRequest &req);

private:
    // Helper: extract last path segment as the resource ID
    std::string get_resource_id(const HttpRequest &req) const;
    // Helper: extract path segment at position
    std::string get_path_segment(const HttpRequest &req, size_t index) const;

    // Token management
    std::string generate_token();
    bool validate_token(const std::string &token);
    void invalidate_token(const std::string &token);

private:
    http_server _server;
    sse_manager _sse;
    redis_adapter *_caster_redis = nullptr;
    redis_adapter *_auth_redis = nullptr;
    HttpApiConfig _config;

    // Active tokens
    std::unordered_set<std::string> _active_tokens;
    std::mutex _token_mutex;
};
