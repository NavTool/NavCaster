#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/sourcetable.h"

namespace navcaster::caster {

struct ClusterSourcetableCacheMetrics {
    std::uint64_t runtime_count = 0;
    std::uint64_t entry_count = 0;
    std::uint64_t age_ms = 0;
};

class ClusterSourcetableCache {
public:
    explicit ClusterSourcetableCache(std::string local_runtime_id, std::uint64_t remote_ttl_ms = 60000);

    void upsert_local_entry(SourcetableEntry entry);
    void remove_local_mount(const std::string &mount);

    std::vector<SourcetableEntry> snapshot_entries() const;
    std::string local_snapshot_json() const;
    bool apply_remote_snapshot_json(const std::string &payload);
    ClusterSourcetableCacheMetrics metrics() const;

    const std::string &local_runtime_id() const { return _local_runtime_id; }
    std::uint64_t version() const;

private:
    struct RuntimeSnapshot {
        bool local = false;
        std::uint64_t updated_at_ms = 0;
        std::unordered_map<std::string, SourcetableEntry> entries;
    };

    bool snapshot_expired(const RuntimeSnapshot &snapshot, std::uint64_t now_ms) const;

    std::string _local_runtime_id;
    std::uint64_t _remote_ttl_ms = 60000;
    mutable std::mutex _mutex;
    std::unordered_map<std::string, RuntimeSnapshot> _snapshots;
    std::uint64_t _version = 0;
};

} // namespace navcaster::caster
