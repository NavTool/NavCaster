#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

#include "domain/position.h"
#include "storage/redis/redis_async_context.h"

struct event_base;

namespace navcaster::caster {

struct WorkerRedisBoundary {
    RedisAsyncContext command;
    RedisAsyncContext pubsub;

    using MountMessageCallback = std::function<void(std::string origin_runtime_id, std::string mount, std::string payload)>;
    using ErrorCallback = std::function<void(std::string operation)>;

    WorkerRedisBoundary() = default;
    WorkerRedisBoundary(std::string runtime_id, std::uint32_t worker_id, const std::string &host, int port);
    ~WorkerRedisBoundary();

    WorkerRedisBoundary(const WorkerRedisBoundary &) = delete;
    WorkerRedisBoundary &operator=(const WorkerRedisBoundary &) = delete;

    bool start(event_base *base, MountMessageCallback on_mount_message, ErrorCallback on_error);
    void stop();

    bool publish_mount_data(const std::string &mount, const std::string &payload);
    bool subscribe_mount(const std::string &mount);
    bool unsubscribe_mount(const std::string &mount);
    bool report_mount_position(const std::string &mount, const GeoPosition &position);
    bool report_client_position(const std::string &member_key, const GeoPosition &position);
    bool connected() const;
    std::uint64_t subscribed_mount_count() const;

    void handle_subscribe_reply(void *reply);
    void handle_publish_reply(void *reply);

    const std::string &runtime_id() const { return _runtime_id; }

private:
    std::string channel_for_mount(const std::string &mount) const;
    void report_error(const std::string &operation);

    std::string _runtime_id;
    std::uint32_t _worker_id = 0;
    MountMessageCallback _on_mount_message;
    ErrorCallback _on_error;
    std::unordered_set<std::string> _subscribed_mounts;
    bool _started = false;
};

} // namespace navcaster::caster
