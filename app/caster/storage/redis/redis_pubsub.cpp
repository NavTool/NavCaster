#include "storage/redis/redis_pubsub.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

#include <async.h>
#include <hiredis.h>

#include "infra/logger.h"

namespace navcaster::caster {
namespace {

constexpr const char *kV2StreamChannelPrefix = "v2:stream:mount:";
constexpr const char *kPayloadMagic = "NCV2BUS1";
constexpr const char *kMountPositionKey = "MPT:GEO";
constexpr const char *kClientPositionKey = "USR:GEO";

std::string encode_payload(const std::string &runtime_id, const std::string &mount, const std::string &payload)
{
    std::string envelope;
    envelope.reserve(
        std::char_traits<char>::length(kPayloadMagic) + runtime_id.size() + mount.size() + payload.size() + 48);
    envelope += kPayloadMagic;
    envelope += '\n';
    envelope += std::to_string(runtime_id.size());
    envelope += '\n';
    envelope += std::to_string(mount.size());
    envelope += '\n';
    envelope += std::to_string(payload.size());
    envelope += '\n';
    envelope += runtime_id;
    envelope += mount;
    envelope += payload;
    return envelope;
}

bool parse_size_line(const std::string &line, std::size_t &out)
{
    if (line.empty() || !std::all_of(line.begin(), line.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        })) {
        return false;
    }

    char *end = nullptr;
    const unsigned long long value = std::strtoull(line.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<std::size_t>(value);
    return true;
}

bool read_line(const std::string &input, std::size_t &offset, std::string &line)
{
    const auto end = input.find('\n', offset);
    if (end == std::string::npos) {
        return false;
    }
    line = input.substr(offset, end - offset);
    offset = end + 1;
    return true;
}

bool decode_payload(const std::string &envelope, std::string &runtime_id, std::string &mount, std::string &payload)
{
    std::size_t offset = 0;
    std::string line;
    if (!read_line(envelope, offset, line) || line != kPayloadMagic) {
        return false;
    }

    std::size_t runtime_len = 0;
    std::size_t mount_len = 0;
    std::size_t payload_len = 0;
    if (!read_line(envelope, offset, line) || !parse_size_line(line, runtime_len) ||
        !read_line(envelope, offset, line) || !parse_size_line(line, mount_len) ||
        !read_line(envelope, offset, line) || !parse_size_line(line, payload_len)) {
        return false;
    }

    if (envelope.size() - offset != runtime_len + mount_len + payload_len) {
        return false;
    }
    runtime_id = envelope.substr(offset, runtime_len);
    offset += runtime_len;
    mount = envelope.substr(offset, mount_len);
    offset += mount_len;
    payload = envelope.substr(offset, payload_len);
    return !runtime_id.empty() && !mount.empty();
}

std::string reply_string(redisReply *reply)
{
    if (!reply || !reply->str || reply->len == 0) {
        return {};
    }
    return std::string(reply->str, reply->len);
}

bool reply_command_equals(redisReply *reply, const char *expected)
{
    std::string command = reply_string(reply);
    std::transform(command.begin(), command.end(), command.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return command == expected;
}

void on_publish(redisAsyncContext *, void *reply, void *privdata)
{
    auto *boundary = static_cast<WorkerRedisBoundary *>(privdata);
    if (boundary) {
        boundary->handle_publish_reply(reply);
    }
}

void on_subscribe(redisAsyncContext *, void *reply, void *privdata)
{
    auto *boundary = static_cast<WorkerRedisBoundary *>(privdata);
    if (boundary) {
        boundary->handle_subscribe_reply(reply);
    }
}

} // namespace

WorkerRedisBoundary::WorkerRedisBoundary(std::string runtime_id, std::uint32_t worker_id, const std::string &host, int port)
    : command("worker-command", host, port),
      pubsub("worker-pubsub", host, port),
      _runtime_id(std::move(runtime_id)),
      _worker_id(worker_id)
{
}

WorkerRedisBoundary::~WorkerRedisBoundary()
{
    stop();
}

bool WorkerRedisBoundary::start(event_base *base, MountMessageCallback on_mount_message, ErrorCallback on_error)
{
    if (_started) {
        return true;
    }

    _on_mount_message = std::move(on_mount_message);
    _on_error = std::move(on_error);

    const bool command_ok = command.connect(base);
    const bool pubsub_ok = pubsub.connect(base);
    _started = command_ok && pubsub_ok;
    if (!command_ok) {
        report_error("connect_command");
    }
    if (!pubsub_ok) {
        report_error("connect_pubsub");
    }
    return _started;
}

void WorkerRedisBoundary::stop()
{
    _subscribed_mounts.clear();
    _on_mount_message = nullptr;
    _on_error = nullptr;
    pubsub.disconnect();
    command.disconnect();
    _started = false;
}

bool WorkerRedisBoundary::publish_mount_data(const std::string &mount, const std::string &payload)
{
    if (mount.empty() || payload.empty() || !command.raw()) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=publish");
        return false;
    }

    const auto channel = channel_for_mount(mount);
    const auto envelope = encode_payload(_runtime_id, mount, payload);
    const int status = redisAsyncCommand(
        command.raw(),
        &on_publish,
        this,
        "PUBLISH %b %b",
        channel.data(),
        channel.size(),
        envelope.data(),
        envelope.size());
    if (status != REDIS_OK) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=publish");
        return false;
    }
    return true;
}

bool WorkerRedisBoundary::subscribe_mount(const std::string &mount)
{
    if (mount.empty()) {
        return false;
    }
    if (_subscribed_mounts.find(mount) != _subscribed_mounts.end()) {
        return true;
    }
    if (!pubsub.raw()) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=subscribe");
        return false;
    }

    const auto channel = channel_for_mount(mount);
    const int status = redisAsyncCommand(
        pubsub.raw(),
        &on_subscribe,
        this,
        "SUBSCRIBE %b",
        channel.data(),
        channel.size());
    if (status != REDIS_OK) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=subscribe");
        return false;
    }
    _subscribed_mounts.insert(mount);
    return true;
}

bool WorkerRedisBoundary::unsubscribe_mount(const std::string &mount)
{
    const auto subscribed = _subscribed_mounts.find(mount);
    if (subscribed == _subscribed_mounts.end()) {
        return true;
    }
    if (!pubsub.raw()) {
        _subscribed_mounts.erase(subscribed);
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=unsubscribe");
        return false;
    }

    const auto channel = channel_for_mount(mount);
    const int status = redisAsyncCommand(
        pubsub.raw(),
        &on_subscribe,
        this,
        "UNSUBSCRIBE %b",
        channel.data(),
        channel.size());
    _subscribed_mounts.erase(subscribed);
    if (status != REDIS_OK) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=unsubscribe");
        return false;
    }
    return true;
}

bool WorkerRedisBoundary::report_mount_position(const std::string &mount, const GeoPosition &position)
{
    if (mount.empty() || !position.valid || !command.raw()) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=report_mount_position");
        return false;
    }

    const int status = redisAsyncCommand(
        command.raw(),
        nullptr,
        nullptr,
        "GEOADD %s %.12f %.12f %b",
        kMountPositionKey,
        position.longitude_deg,
        position.latitude_deg,
        mount.data(),
        mount.size());
    if (status != REDIS_OK) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=report_mount_position");
        return false;
    }
    return true;
}

bool WorkerRedisBoundary::report_client_position(const std::string &connect_key, const GeoPosition &position)
{
    if (connect_key.empty() || !position.valid || !command.raw()) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=report_client_position");
        return false;
    }

    const int status = redisAsyncCommand(
        command.raw(),
        nullptr,
        nullptr,
        "GEOADD %s %.12f %.12f %b",
        kClientPositionKey,
        position.longitude_deg,
        position.latitude_deg,
        connect_key.data(),
        connect_key.size());
    if (status != REDIS_OK) {
        log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=report_client_position");
        return false;
    }
    return true;
}

bool WorkerRedisBoundary::connected() const
{
    return command.connected() && pubsub.connected();
}

std::uint64_t WorkerRedisBoundary::subscribed_mount_count() const
{
    return static_cast<std::uint64_t>(_subscribed_mounts.size());
}

void WorkerRedisBoundary::handle_publish_reply(void *reply)
{
    auto *redis_reply = static_cast<redisReply *>(reply);
    if (!redis_reply || redis_reply->type == REDIS_REPLY_ERROR) {
        report_error("publish");
    }
}

void WorkerRedisBoundary::handle_subscribe_reply(void *reply)
{
    auto *redis_reply = static_cast<redisReply *>(reply);
    if (!redis_reply || redis_reply->type == REDIS_REPLY_ERROR) {
        report_error("subscribe");
        return;
    }
    if (redis_reply->type != REDIS_REPLY_ARRAY || redis_reply->elements < 3 || !redis_reply->element[0]) {
        return;
    }

    redisReply *kind = redis_reply->element[0];
    if (reply_command_equals(kind, "subscribe") || reply_command_equals(kind, "unsubscribe")) {
        return;
    }
    if (!reply_command_equals(kind, "message")) {
        return;
    }

    redisReply *message = redis_reply->element[2];
    if (!message || !message->str || message->len == 0) {
        report_error("decode");
        return;
    }

    std::string origin_runtime_id;
    std::string mount;
    std::string payload;
    if (!decode_payload(std::string(message->str, message->len), origin_runtime_id, mount, payload)) {
        report_error("decode");
        return;
    }

    if (origin_runtime_id == _runtime_id) {
        return;
    }
    if (_on_mount_message) {
        _on_mount_message(std::move(origin_runtime_id), std::move(mount), std::move(payload));
    }
}

std::string WorkerRedisBoundary::channel_for_mount(const std::string &mount) const
{
    return std::string(kV2StreamChannelPrefix) + mount;
}

void WorkerRedisBoundary::report_error(const std::string &operation)
{
    if (_on_error) {
        _on_error(operation);
    }
    log_warn("v2 redis bus worker=" + std::to_string(_worker_id) + " operation=" + operation);
}

} // namespace navcaster::caster
