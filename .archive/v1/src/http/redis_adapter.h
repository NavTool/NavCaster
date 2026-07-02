#pragma once

#include <event2/event.h>
#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <memory>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Callback for async Redis operations: (success, result_json)
using RedisResultCallback = std::function<void(bool success, const json &result)>;

// Pending redis request context, passed as privdata to redisAsyncCommand
struct RedisRequestContext
{
    RedisResultCallback callback;
    std::string operation; // for logging: "HGETALL", "HGET", etc.
    std::string key;
};

class redis_adapter
{
public:
    redis_adapter();
    ~redis_adapter();

    // Initialize with existing Redis async context from caster_core
    // If host/port/password are provided, creates a dedicated connection
    int init(event_base *base, const std::string &host, int port, const std::string &password);

    bool is_connected() const { return _connected; }

    // Hash operations with async callbacks
    void hash_get_all(const std::string &key, RedisResultCallback cb);
    void hash_get(const std::string &key, const std::string &field, RedisResultCallback cb);
    void hash_set(const std::string &key, const std::string &field, const std::string &value, RedisResultCallback cb);
    void hash_set_nx(const std::string &key, const std::string &field, const std::string &value, RedisResultCallback cb);
    void hash_del(const std::string &key, const std::string &field, RedisResultCallback cb);

    // Generic command
    void command(const std::string &cmd, RedisResultCallback cb);

private:
    static void connect_callback(const redisAsyncContext *c, int status);
    static void disconnect_callback(const redisAsyncContext *c, int status);
    static void reconnect_callback(evutil_socket_t fd, short what, void *arg);

    // Redis command callbacks
    static void hgetall_callback(redisAsyncContext *c, void *r, void *privdata);
    static void hget_callback(redisAsyncContext *c, void *r, void *privdata);
    static void hset_callback(redisAsyncContext *c, void *r, void *privdata);
    static void hdel_callback(redisAsyncContext *c, void *r, void *privdata);
    static void generic_callback(redisAsyncContext *c, void *r, void *privdata);

    void execute_pending();
    int connect_async();
    void schedule_reconnect();

private:
    event_base *_base = nullptr;
    redisAsyncContext *_ctx = nullptr;
    event *_reconnect_event = nullptr;
    bool _connected = false;
    bool _reconnect_scheduled = false;
    bool _shutting_down = false;
    int _reconnect_attempts = 0;

    std::string _host;
    int _port = 6379;
    std::string _password;

    // Queue commands if not yet connected
    struct PendingCommand
    {
        std::string cmd;
        RedisResultCallback callback;
        void (*redis_cb)(redisAsyncContext *, void *, void *);
    };
    std::queue<PendingCommand> _pending;
};
