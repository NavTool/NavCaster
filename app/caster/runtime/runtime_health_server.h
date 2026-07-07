#pragma once

#include <functional>
#include <string>

#include <event2/http.h>

#include "runtime/runtime_config.h"
#include "runtime/runtime_metrics.h"

namespace navcaster::caster {

class RuntimeHealthServer {
public:
    using SnapshotProvider = std::function<RuntimeMetricsSnapshot()>;

    RuntimeHealthServer(RuntimeConfig config, SnapshotProvider snapshot_provider);
    ~RuntimeHealthServer();

    RuntimeHealthServer(const RuntimeHealthServer &) = delete;
    RuntimeHealthServer &operator=(const RuntimeHealthServer &) = delete;

    bool start(event_base *base);
    void stop();
    bool running() const { return _http != nullptr; }

private:
    static void on_request(evhttp_request *request, void *arg);
    void handle_request(evhttp_request *request);
    void send_json(evhttp_request *request, int status, const std::string &body);

    RuntimeConfig _config;
    SnapshotProvider _snapshot_provider;
    evhttp *_http = nullptr;
};

} // namespace navcaster::caster
