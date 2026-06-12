#pragma once

#include "access_group.h"
#include "access_item.h"
#include "source_record.h"

#include <string>
#include <unordered_map>

namespace navcaster::core
{
constexpr const char *DEFAULT_ACCESS_GROUP = "default";
constexpr const char *SYSTEM_ACCESS_GROUP = "SYSTEM";

using SourceRecordMap = std::unordered_map<std::string, source_record>;
using AccessGroupMap = std::unordered_map<std::string, access_group>;
using AccessItemMap = std::unordered_map<std::string, std::unordered_map<std::string, access_item>>;

std::string normalize_access_group_uid(const char *group_uid);
std::string normalize_access_group_uid(const std::string &group_uid);
bool is_privileged_access_group(const std::string &group_uid);

class AccessPolicyService
{
public:
    AccessPolicyService(const SourceRecordMap &source_records,
                        const SourceRecordMap &source_decodes,
                        const AccessGroupMap &access_groups,
                        const AccessItemMap &access_items);

    bool is_nearest_mount(const std::string &mount_point) const;
    std::string resolve_mount_group(const std::string &mount_point) const;
    bool is_mount_inside_group(const std::string &group_uid, const std::string &mount_point) const;

    bool check_nearest_mount_login(const std::string &group_uid,
                                   const std::string &mount_point,
                                   std::string *reason = nullptr) const;
    bool check_mount_visible(const std::string &group_uid,
                             const std::string &mount_point,
                             std::string *reason = nullptr) const;
    bool check_mount_access(const std::string &group_uid,
                            const std::string &mount_point,
                            std::string *reason = nullptr) const;
    bool check_mount_nearby(const std::string &group_uid,
                            const std::string &mount_point,
                            std::string *reason = nullptr) const;

private:
    const SourceRecordMap &_source_records;
    const SourceRecordMap &_source_decodes;
    const AccessGroupMap &_access_groups;
    const AccessItemMap &_access_items;
};
} // namespace navcaster::core
