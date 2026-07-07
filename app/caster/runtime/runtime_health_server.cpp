#include "runtime/runtime_health_server.h"

#include <utility>

#include <event2/buffer.h>
#include <event2/http.h>
#include <nlohmann/json.hpp>

#include "infra/logger.h"

namespace navcaster::caster {
namespace {

using Json = nlohmann::ordered_json;

std::string health_json(const RuntimeMetricsSnapshot &snapshot)
{
    return Json{
        {"ok", snapshot.running},
        {"runtime_id", snapshot.runtime_id},
    }.dump();
}

std::string error_json(const std::string &error)
{
    return Json{
        {"error", error},
    }.dump();
}

} // namespace

RuntimeHealthServer::RuntimeHealthServer(RuntimeConfig config, SnapshotProvider snapshot_provider)
    : _config(std::move(config)), _snapshot_provider(std::move(snapshot_provider))
{
}

RuntimeHealthServer::~RuntimeHealthServer()
{
    stop();
}

bool RuntimeHealthServer::start(event_base *base)
{
    if (!base || _http) {
        return _http != nullptr;
    }

    _http = evhttp_new(base);
    if (!_http) {
        log_error("failed to create runtime health server");
        return false;
    }

    evhttp_set_gencb(_http, &RuntimeHealthServer::on_request, this);
    if (evhttp_bind_socket(_http, _config.health_host.c_str(), _config.health_port) != 0) {
        log_error("failed to bind runtime health server on " + _config.health_host + ":" + std::to_string(_config.health_port));
        stop();
        return false;
    }
    return true;
}

void RuntimeHealthServer::stop()
{
    if (_http) {
        evhttp_free(_http);
        _http = nullptr;
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
    const auto snapshot = _snapshot_provider ? _snapshot_provider() : RuntimeMetricsSnapshot{};

    if (path == "/health" || path == "/healthz" || path == "/api/v1/runtime-local/health") {
        send_json(request, snapshot.running ? 200 : 503, health_json(snapshot));
        return;
    }
    if (path == "/metrics" || path == "/metrics.json" || path == "/api/v1/runtime-local/metrics") {
        send_json(request, 200, runtime_metrics_to_json(snapshot));
        return;
    }

    send_json(request, 404, error_json("not_found"));
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
