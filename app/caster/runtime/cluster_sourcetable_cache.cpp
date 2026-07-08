#include "runtime/cluster_sourcetable_cache.h"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

#include "infra/timer.h"

namespace navcaster::caster {
namespace {

using Json = nlohmann::ordered_json;

Json position_to_json(const GeoPosition &position)
{
    return Json{
        {"valid", position.valid},
        {"latitude_deg", position.latitude_deg},
        {"longitude_deg", position.longitude_deg},
        {"height_m", position.height_m},
        {"ecef_x_m", position.ecef_x_m},
        {"ecef_y_m", position.ecef_y_m},
        {"ecef_z_m", position.ecef_z_m},
        {"updated_at_ms", position.updated_at_ms},
    };
}

GeoPosition position_from_json(const Json &json)
{
    GeoPosition position;
    if (!json.is_object()) {
        return position;
    }
    position.valid = json.value("valid", false);
    position.latitude_deg = json.value("latitude_deg", 0.0);
    position.longitude_deg = json.value("longitude_deg", 0.0);
    position.height_m = json.value("height_m", 0.0);
    position.ecef_x_m = json.value("ecef_x_m", 0.0);
    position.ecef_y_m = json.value("ecef_y_m", 0.0);
    position.ecef_z_m = json.value("ecef_z_m", 0.0);
    position.updated_at_ms = json.value("updated_at_ms", std::uint64_t{0});
    return position;
}

Json entry_to_json(const SourcetableEntry &entry)
{
    return Json{
        {"mount", entry.mount},
        {"identifier", entry.identifier},
        {"format", entry.format},
        {"format_details", entry.format_details},
        {"carrier", entry.carrier},
        {"nav_system", entry.nav_system},
        {"network", entry.network},
        {"country", entry.country},
        {"nmea_required", entry.nmea_required},
        {"solution", entry.solution},
        {"generator", entry.generator},
        {"compression", entry.compression},
        {"authentication", entry.authentication},
        {"fee", entry.fee},
        {"bitrate", entry.bitrate},
        {"misc", entry.misc},
        {"position", position_to_json(entry.position)},
    };
}

SourcetableEntry entry_from_json(const Json &json)
{
    SourcetableEntry entry;
    if (!json.is_object()) {
        return entry;
    }
    entry.mount = json.value("mount", std::string{});
    entry.identifier = json.value("identifier", entry.mount);
    entry.format = json.value("format", entry.format);
    entry.format_details = json.value("format_details", entry.format_details);
    entry.carrier = json.value("carrier", entry.carrier);
    entry.nav_system = json.value("nav_system", entry.nav_system);
    entry.network = json.value("network", entry.network);
    entry.country = json.value("country", entry.country);
    entry.nmea_required = json.value("nmea_required", entry.nmea_required);
    entry.solution = json.value("solution", entry.solution);
    entry.generator = json.value("generator", entry.generator);
    entry.compression = json.value("compression", entry.compression);
    entry.authentication = json.value("authentication", entry.authentication);
    entry.fee = json.value("fee", entry.fee);
    entry.bitrate = json.value("bitrate", entry.bitrate);
    entry.misc = json.value("misc", entry.misc);
    entry.position = position_from_json(json.value("position", Json::object()));
    return entry;
}

void sort_entries(std::vector<SourcetableEntry> &entries)
{
    std::sort(entries.begin(), entries.end(), [](const SourcetableEntry &left, const SourcetableEntry &right) {
        if (left.mount == right.mount) {
            return left.misc < right.misc;
        }
        return left.mount < right.mount;
    });
}

} // namespace

ClusterSourcetableCache::ClusterSourcetableCache(std::string local_runtime_id, std::uint64_t remote_ttl_ms)
    : _local_runtime_id(std::move(local_runtime_id)), _remote_ttl_ms(remote_ttl_ms)
{
}

void ClusterSourcetableCache::upsert_local_entry(SourcetableEntry entry)
{
    if (entry.mount.empty()) {
        return;
    }
    if (entry.identifier.empty()) {
        entry.identifier = entry.mount;
    }
    if (entry.misc.empty() || entry.misc == "runtime") {
        entry.misc = _local_runtime_id;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    auto &snapshot = _snapshots[_local_runtime_id];
    snapshot.local = true;
    snapshot.updated_at_ms = steady_time_ms();
    snapshot.entries[entry.mount] = std::move(entry);
    ++_version;
}

void ClusterSourcetableCache::remove_local_mount(const std::string &mount)
{
    if (mount.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);
    auto &snapshot = _snapshots[_local_runtime_id];
    snapshot.local = true;
    snapshot.updated_at_ms = steady_time_ms();
    if (snapshot.entries.erase(mount) > 0) {
        ++_version;
    }
}

std::vector<SourcetableEntry> ClusterSourcetableCache::snapshot_entries() const
{
    const auto now_ms = steady_time_ms();
    std::vector<SourcetableEntry> entries;
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &runtime : _snapshots) {
        const auto &snapshot = runtime.second;
        if (snapshot_expired(snapshot, now_ms)) {
            continue;
        }
        for (const auto &entry : snapshot.entries) {
            entries.push_back(entry.second);
        }
    }
    sort_entries(entries);
    return entries;
}

std::string ClusterSourcetableCache::local_snapshot_json() const
{
    Json entries = Json::array();
    std::lock_guard<std::mutex> lock(_mutex);
    const auto it = _snapshots.find(_local_runtime_id);
    if (it != _snapshots.end()) {
        std::vector<SourcetableEntry> sorted_entries;
        sorted_entries.reserve(it->second.entries.size());
        for (const auto &entry : it->second.entries) {
            sorted_entries.push_back(entry.second);
        }
        sort_entries(sorted_entries);
        for (const auto &entry : sorted_entries) {
            entries.push_back(entry_to_json(entry));
        }
    }

    return Json{
        {"runtime_id", _local_runtime_id},
        {"published_at_ms", steady_time_ms()},
        {"entry_count", entries.size()},
        {"entries", std::move(entries)},
    }.dump();
}

bool ClusterSourcetableCache::apply_remote_snapshot_json(const std::string &payload)
{
    Json json;
    try {
        json = Json::parse(payload);
    } catch (const std::exception &) {
        return false;
    }
    if (!json.is_object()) {
        return false;
    }

    const auto runtime_id = json.value("runtime_id", std::string{});
    if (runtime_id.empty()) {
        return false;
    }
    if (runtime_id == _local_runtime_id) {
        return true;
    }

    RuntimeSnapshot snapshot;
    snapshot.local = false;
    snapshot.updated_at_ms = steady_time_ms();
    const auto entries_json = json.value("entries", Json::array());
    if (!entries_json.is_array()) {
        return false;
    }
    for (const auto &entry_json : entries_json) {
        auto entry = entry_from_json(entry_json);
        if (!entry.mount.empty()) {
            snapshot.entries[entry.mount] = std::move(entry);
        }
    }

    std::lock_guard<std::mutex> lock(_mutex);
    _snapshots[runtime_id] = std::move(snapshot);
    return true;
}

ClusterSourcetableCacheMetrics ClusterSourcetableCache::metrics() const
{
    const auto now_ms = steady_time_ms();
    ClusterSourcetableCacheMetrics metrics;
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &runtime : _snapshots) {
        const auto &snapshot = runtime.second;
        if (snapshot_expired(snapshot, now_ms)) {
            continue;
        }
        ++metrics.runtime_count;
        metrics.entry_count += static_cast<std::uint64_t>(snapshot.entries.size());
        if (!snapshot.entries.empty()) {
            metrics.age_ms = std::max(metrics.age_ms, now_ms - snapshot.updated_at_ms);
        }
    }
    return metrics;
}

std::uint64_t ClusterSourcetableCache::version() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _version;
}

bool ClusterSourcetableCache::snapshot_expired(const RuntimeSnapshot &snapshot, std::uint64_t now_ms) const
{
    return !snapshot.local && snapshot.updated_at_ms > 0 && now_ms > snapshot.updated_at_ms &&
           now_ms - snapshot.updated_at_ms > _remote_ttl_ms;
}

} // namespace navcaster::caster
