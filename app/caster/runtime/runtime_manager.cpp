#include "runtime/runtime_manager.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>

#include <event2/event.h>

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

} // namespace

RuntimeManager::RuntimeManager(RuntimeConfig config)
    : config_(std::move(config))
{
}

RuntimeManager::~RuntimeManager()
{
    stop();
}

bool RuntimeManager::start()
{
    if (running_.load()) {
        return true;
    }

    base_ = make_event_base();
    if (!base_) {
        log_error("runtime manager failed to create control event_base");
        return false;
    }

    worker_manager_ = std::make_unique<WorkerManager>(config_);
    if (!worker_manager_->start()) {
        stop();
        return false;
    }

    if (config_.enable_health_api) {
        health_server_ = std::make_unique<RuntimeHealthServer>(config_, [this]() {
            return metrics_snapshot();
        });
        if (!health_server_->start(base_.get())) {
            stop();
            return false;
        }
    }

    if (config_.enable_acceptor) {
        acceptor_ = std::make_unique<Acceptor>(config_, [this](HandoffMessage message) {
            return dispatch_handoff(std::move(message));
        });
        if (!acceptor_->start(base_.get())) {
            stop();
            return false;
        }
    }

    started_at_ = std::chrono::steady_clock::now();
    running_.store(true);
    runtime_thread_ = std::thread(&RuntimeManager::runtime_loop, this);
    return true;
}

void RuntimeManager::stop()
{
    running_.store(false);
    if (base_) {
        event_base_loopbreak(base_.get());
    }
    if (runtime_thread_.joinable()) {
        runtime_thread_.join();
    }

    acceptor_.reset();
    health_server_.reset();
    if (worker_manager_) {
        worker_manager_->stop();
        worker_manager_.reset();
    }
    base_.reset();
}

RuntimeMetricsSnapshot RuntimeManager::metrics_snapshot() const
{
    RuntimeMetricsSnapshot snapshot;
    snapshot.runtime_id = config_.runtime_id;
    snapshot.running = running_.load();
    if (snapshot.running) {
        const auto elapsed = std::chrono::steady_clock::now() - started_at_;
        snapshot.uptime_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }
    if (worker_manager_) {
        snapshot.workers = worker_manager_->metrics_snapshot();
        snapshot.worker_count = static_cast<std::uint32_t>(snapshot.workers.size());
        snapshot.mount_count = worker_manager_->mount_owners().mount_count();
        snapshot.mount_owners = worker_manager_->mount_owners().snapshot();
    }
    return snapshot;
}

RuntimeSelfTestResult RuntimeManager::run_self_test()
{
    RuntimeSelfTestResult result;
    if (!start()) {
        result.report_json = "{\"ok\":false,\"error\":\"start_failed\"}";
        return result;
    }

    bool handoff_ok = false;
    if (worker_manager_) {
        const auto owner_id = worker_manager_->mount_owners().resolve_or_assign("SELFTEST", worker_manager_->metrics_snapshot());
        handoff_ok = owner_id != 0;
    }
    const bool probes_ok = worker_manager_ && worker_manager_->post_probe_to_all();

    if (worker_manager_) {
        worker_manager_->set_worker_draining(1, true);
        worker_manager_->set_worker_draining(1, false);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(config_.self_test_duration_ms));
    const auto snapshot = metrics_snapshot();

    bool workers_ok = snapshot.worker_count == config_.worker_count;
    for (const auto &worker : snapshot.workers) {
        workers_ok = workers_ok && worker.running && worker.mailbox_messages > 0;
    }

    result.ok = handoff_ok && probes_ok && workers_ok && snapshot.mount_count == 1;
    std::ostringstream out;
    out << "{\"ok\":" << (result.ok ? "true" : "false")
        << ",\"handoff_ok\":" << (handoff_ok ? "true" : "false")
        << ",\"probes_ok\":" << (probes_ok ? "true" : "false")
        << ",\"snapshot\":" << runtime_metrics_to_json(snapshot)
        << "}";
    result.report_json = out.str();

    stop();
    return result;
}

void RuntimeManager::runtime_loop()
{
    if (base_) {
        event_base_dispatch(base_.get());
    }
}

bool RuntimeManager::dispatch_handoff(HandoffMessage message)
{
    if (message.connect_info.type == ConnectType::SourceTable) {
        return respond_source_table(std::move(message));
    }

    if (!worker_manager_) {
        if (message.fd >= 0) {
            close_socket(message.fd);
        }
        return false;
    }

    return worker_manager_->dispatch_handoff(std::move(message));
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
