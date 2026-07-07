#include "runtime/runtime_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>

#include <event2/event.h>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#endif

#include "domain/connect_info.h"
#include "domain/http_chunked_codec.h"
#include "domain/sourcetable.h"
#include "infra/logger.h"
#include "infra/socket_util.h"

namespace navcaster::caster {
namespace {

using Json = nlohmann::ordered_json;

std::vector<SourcetableEntry> sourcetable_entries_from_metrics(const RuntimeMetricsSnapshot &snapshot)
{
    std::vector<SourcetableEntry> entries;
    for (const auto &worker : snapshot.workers) {
        for (const auto &mount : worker.mounts) {
            if (!mount.source_online || mount.mount.empty()) {
                continue;
            }
            SourcetableEntry entry;
            entry.mount = mount.mount;
            entry.identifier = mount.mount;
            entry.position = mount.base_position;
            entries.push_back(std::move(entry));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const SourcetableEntry &left, const SourcetableEntry &right) {
        return left.mount < right.mount;
    });
    return entries;
}

bool send_all_and_close(evutil_socket_t fd, const std::string &payload)
{
    if (fd < 0) {
        return false;
    }

#if defined(_WIN32)
    u_long nonblocking = 0;
    ioctlsocket(fd, FIONBIO, &nonblocking);
#else
    const int flags_current = fcntl(fd, F_GETFL, 0);
    if (flags_current >= 0) {
        fcntl(fd, F_SETFL, flags_current & ~O_NONBLOCK);
    }
#endif
    std::size_t sent = 0;
    bool ok = true;
    while (sent < payload.size()) {
#if defined(_WIN32)
        const int chunk = static_cast<int>(std::min<std::size_t>(payload.size() - sent, 64 * 1024));
        const int written = ::send(fd, payload.data() + sent, chunk, 0);
#else
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
        const auto written = ::send(fd, payload.data() + sent, payload.size() - sent, flags);
#endif
        if (written <= 0) {
            ok = false;
            break;
        }
        sent += static_cast<std::size_t>(written);
    }
    close_socket(fd);
    return ok;
}

std::string build_source_table_response(const ConnectInfo &info, const std::string &body)
{
    if (info.ntrip2) {
        const bool chunked = info.accepts_chunked_response;
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Server: NavCaster\r\n"
                 << "Ntrip-Version: Ntrip/2.0\r\n"
                 << "Content-Type: text/plain\r\n";
        if (chunked) {
            response << "Transfer-Encoding: chunked\r\n";
        } else {
            response << "Content-Length: " << body.size() << "\r\n";
        }
        response << "Connection: close\r\n"
                 << "\r\n";
        if (chunked) {
            response << encode_http_chunk(body) << encode_http_last_chunk();
        } else {
            response << body;
        }
        return response.str();
    }

    std::ostringstream response;
    response << "SOURCETABLE 200 OK\r\n"
             << "Server: NavCaster\r\n"
             << "Content-Type: text/plain\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;
    return response.str();
}

std::string self_test_start_failed_report()
{
    return Json{
        {"ok", false},
        {"error", "start_failed"},
    }.dump();
}

std::string self_test_report_json(bool ok, bool handoff_ok, bool probes_ok, const RuntimeMetricsSnapshot &snapshot)
{
    return Json{
        {"ok", ok},
        {"handoff_ok", handoff_ok},
        {"probes_ok", probes_ok},
        {"snapshot", Json::parse(runtime_metrics_to_json(snapshot))},
    }.dump();
}

} // namespace

RuntimeManager::RuntimeManager(RuntimeConfig config)
    : _config(std::move(config))
{
}

RuntimeManager::~RuntimeManager()
{
    stop();
}

bool RuntimeManager::start()
{
    if (_running.load()) {
        return true;
    }

    _base = make_event_base();
    if (!_base) {
        log_error("runtime manager failed to create control event_base");
        return false;
    }

    _worker_manager = std::make_unique<WorkerManager>(_config);
    if (!_worker_manager->start()) {
        stop();
        return false;
    }

    if (_config.enable_health_api) {
        _health_server = std::make_unique<RuntimeHealthServer>(_config, [this]() {
            return metrics_snapshot();
        });
        if (!_health_server->start(_base.get())) {
            stop();
            return false;
        }
    }

    if (_config.enable_acceptor) {
        _acceptor = std::make_unique<Acceptor>(_config, [this](HandoffMessage message) {
            return dispatch_handoff(std::move(message));
        });
        if (!_acceptor->start(_base.get())) {
            stop();
            return false;
        }
    }

    _started_at = std::chrono::steady_clock::now();
    _running.store(true);
    _runtime_thread = std::thread(&RuntimeManager::runtime_loop, this);
    return true;
}

void RuntimeManager::stop()
{
    _running.store(false);
    if (_base) {
        event_base_loopbreak(_base.get());
    }
    if (_runtime_thread.joinable()) {
        _runtime_thread.join();
    }

    _acceptor.reset();
    _health_server.reset();
    if (_worker_manager) {
        _worker_manager->stop();
        _worker_manager.reset();
    }
    _base.reset();
}

RuntimeMetricsSnapshot RuntimeManager::metrics_snapshot() const
{
    RuntimeMetricsSnapshot snapshot;
    snapshot.runtime_id = _config.runtime_id;
    snapshot.running = _running.load();
    if (snapshot.running) {
        const auto elapsed = std::chrono::steady_clock::now() - _started_at;
        snapshot.uptime_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
    if (_worker_manager) {
        snapshot.workers = _worker_manager->metrics_snapshot();
        snapshot.worker_count = static_cast<std::uint32_t>(snapshot.workers.size());
        snapshot.mount_count = _worker_manager->mount_owners().mount_count();
        snapshot.mount_owners = _worker_manager->mount_owners().snapshot();
    }
    return snapshot;
}

RuntimeSelfTestResult RuntimeManager::run_self_test()
{
    RuntimeSelfTestResult result;
    if (!start()) {
        result.report_json = self_test_start_failed_report();
        return result;
    }

    bool handoff_ok = false;
    if (_worker_manager) {
        const auto owner_id = _worker_manager->mount_owners().resolve_or_assign("SELFTEST", _worker_manager->metrics_snapshot());
        handoff_ok = owner_id != 0;
    }
    const bool probes_ok = _worker_manager && _worker_manager->post_probe_to_all();

    if (_worker_manager) {
        _worker_manager->set_worker_draining(1, true);
        _worker_manager->set_worker_draining(1, false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(_config.self_test_duration_ms));
    const auto snapshot = metrics_snapshot();

    bool workers_ok = snapshot.worker_count == _config.worker_count;
    for (const auto &worker : snapshot.workers) {
        workers_ok = workers_ok && worker.running && worker.mailbox_messages > 0;
    }

    result.ok = handoff_ok && probes_ok && workers_ok && snapshot.mount_count == 1;
    result.report_json = self_test_report_json(result.ok, handoff_ok, probes_ok, snapshot);

    stop();
    return result;
}

void RuntimeManager::runtime_loop()
{
    if (_base) {
        event_base_dispatch(_base.get());
    }
}

bool RuntimeManager::dispatch_handoff(HandoffMessage message)
{
    if (message.connect_info.type == ConnectType::SourceTable) {
        return respond_source_table(std::move(message));
    }

    if (!_worker_manager) {
        if (message.fd >= 0) {
            close_socket(message.fd);
        }
        return false;
    }

    return _worker_manager->dispatch_handoff(std::move(message));
}

bool RuntimeManager::respond_source_table(HandoffMessage message)
{
    const auto entries = sourcetable_entries_from_metrics(metrics_snapshot());
    const auto body = build_sourcetable(entries);
    const auto response = build_source_table_response(message.connect_info, body);
    const auto fd = message.fd;
    message.fd = -1;
    const bool ok = send_all_and_close(fd, response);
    if (!ok) {
        log_warn("failed to write source table response");
    }
    return ok;
}

} // namespace navcaster::caster
