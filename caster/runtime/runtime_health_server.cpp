#include "runtime/runtime_health_server.h"

#include <utility>

#include <event2/buffer.h>
#include <event2/http.h>

#include "infra/logger.h"

namespace navcaster::caster {

RuntimeHealthServer::RuntimeHealthServer(RuntimeConfig config, SnapshotProvider snapshot_provider)
    : config_(std::move(config)), snapshot_provider_(std::move(snapshot_provider))
{
}

RuntimeHealthServer::~RuntimeHealthServer()
{
    stop();
}

bool RuntimeHealthServer::start(event_base *base)
{
    if (!base || http_) {
        return http_ != nullptr;
    }

    http_ = evhttp_new(base);
    if (!http_) {
        log_error("failed to create runtime health server");
        return false;
    }

    evhttp_set_gencb(http_, &RuntimeHealthServer::on_request, this);
    if (evhttp_bind_socket(http_, config_.health_host.c_str(), config_.health_port) != 0) {
        log_error("failed to bind runtime health server on " + config_.health_host + ":" + std::to_string(config_.health_port));
        stop();
        return false;
    }
    return true;
}

void RuntimeHealthServer::stop()
{
    if (http_) {
        evhttp_free(http_);
        http_ = nullptr;
    }
}

void RuntimeHealthServer::on_request(evhttp_request *request, void *arg)
{
    auto *server = static_cast<RuntimeHealthServer *>(arg);
    if (server) {
        server->handle_request(request);
    }
}

void RuntimeHealthServer::handle_request(evhttp_request *request)
{
    const char *uri = evhttp_request_get_uri(request);
    const std::string path = uri ? uri : "/";
    const auto snapshot = snapshot_provider_ ? snapshot_provider_() : RuntimeMetricsSnapshot{};

    if (path == "/health" || path == "/healthz" || path == "/api/v1/runtime-local/health") {
        const std::string body = std::string("{\"ok\":") + (snapshot.running ? "true" : "false") +
            ",\"runtime_id\":\"" + snapshot.runtime_id + "\"}";
        send_json(request, snapshot.running ? 200 : 503, body);
        return;
    }
    if (path == "/metrics" || path == "/metrics.json" || path == "/api/v1/runtime-local/metrics") {
        send_json(request, 200, runtime_metrics_to_json(snapshot));
        return;
    }

    send_json(request, 404, "{\"error\":\"not_found\"}");
}

void RuntimeHealthServer::send_json(evhttp_request *request, int status, const std::string &body)
{
    evbuffer *buffer = evbuffer_new();
    if (!buffer) {
        evhttp_send_error(request, 500, "buffer allocation failed");
        return;
    }
    evhttp_add_header(evhttp_request_get_output_headers(request), "Content-Type", "application/json");
    evbuffer_add(buffer, body.data(), body.size());
    evhttp_send_reply(request, status, nullptr, buffer);
    evbuffer_free(buffer);
}

} // namespace navcaster::caster
