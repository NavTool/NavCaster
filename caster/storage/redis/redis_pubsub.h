#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>

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
    std::uint64_t subscribed_mount_count() const;

    void handle_subscribe_reply(void *reply);
    void handle_publish_reply(void *reply);

    const std::string &runtime_id() const { return runtime_id_; }

private:
    std::string channel_for_mount(const std::string &mount) const;
    void report_error(const std::string &operation);

    std::string runtime_id_;
    std::uint32_t worker_id_ = 0;
    MountMessageCallback on_mount_message_;
    ErrorCallback on_error_;
    std::unordered_set<std::string> subscribed_mounts_;
    bool started_ = false;
};

} // namespace navcaster::caster
