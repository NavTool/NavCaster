#include "access_policy_service.h"

namespace navcaster::core
{
std::string normalize_access_group_uid(const char *group_uid)
{
    if (group_uid == nullptr || group_uid[0] == '\0')
    {
        return DEFAULT_ACCESS_GROUP;
    }
    return group_uid;
}

std::string normalize_access_group_uid(const std::string &group_uid)
{
    return group_uid.empty() ? DEFAULT_ACCESS_GROUP : group_uid;
}

bool is_privileged_access_group(const std::string &group_uid)
{
    return group_uid == SYSTEM_ACCESS_GROUP;
}

AccessPolicyService::AccessPolicyService(const SourceRecordMap &source_records,
                                         const SourceRecordMap &source_decodes,
                                         const AccessGroupMap &access_groups,
                                         const AccessItemMap &access_items)
    : _source_records(source_records),
      _source_decodes(source_decodes),
      _access_groups(access_groups),
      _access_items(access_items)
{
}

bool AccessPolicyService::is_nearest_mount(const std::string &mount_point) const
{
    for (const auto &group_policy : _access_groups)
    {
        if (group_policy.second.nearest_mpt_enable() &&
            !group_policy.second.nearest_mpt_source_name().empty() &&
            group_policy.second.nearest_mpt_source_name() == mount_point)
        {
            return true;
        }
    }

    return false;
}

std::string AccessPolicyService::resolve_mount_group(const std::string &mount_point) const
{
    auto record = _source_records.find(mount_point);
    if (record != _source_records.end() && record->second.source_group_uid() != DEFAULT_ACCESS_GROUP)
    {
        return record->second.source_group_uid();
    }

    auto decode = _source_decodes.find(mount_point);
    if (decode != _source_decodes.end() && decode->second.source_group_uid() != DEFAULT_ACCESS_GROUP)
    {
        return decode->second.source_group_uid();
    }

    for (const auto &group_items : _access_items)
    {
        if (group_items.second.find(mount_point) != group_items.second.end())
        {
            return group_items.first;
        }
    }

    for (const auto &group_policy : _access_groups)
    {
        if (group_policy.second.nearest_mpt_enable() &&
            !group_policy.second.nearest_mpt_source_name().empty() &&
            group_policy.second.nearest_mpt_source_name() == mount_point)
        {
            return group_policy.first;
        }
    }

    return DEFAULT_ACCESS_GROUP;
}

bool AccessPolicyService::is_mount_inside_group(const std::string &group_uid, const std::string &mount_point) const
{
    const auto group = normalize_access_group_uid(group_uid);

    auto record = _source_records.find(mount_point);
    if (record != _source_records.end() && record->second.source_group_uid() == group)
    {
        return true;
    }

    auto decode = _source_decodes.find(mount_point);
    if (decode != _source_decodes.end() && decode->second.source_group_uid() == group)
    {
        return true;
    }

    auto group_items = _access_items.find(group);
    if (group_items != _access_items.end() && group_items->second.find(mount_point) != group_items->second.end())
    {
        return true;
    }

    auto group_policy = _access_groups.find(group);
    if (group_policy != _access_groups.end() &&
        group_policy->second.nearest_mpt_enable() &&
        group_policy->second.nearest_mpt_source_name() == mount_point)
    {
        return true;
    }

    return resolve_mount_group(mount_point) == group;
}

bool AccessPolicyService::check_nearest_mount_login(const std::string &group_uid,
                                                    const std::string &mount_point,
                                                    std::string *reason) const
{
    if (_access_groups.empty())
    {
        if (reason)
        {
            *reason = "Access group policy not loaded";
        }
        return false;
    }

    const auto group = normalize_access_group_uid(group_uid);
    if (is_privileged_access_group(group))
    {
        return is_nearest_mount(mount_point);
    }

    auto group_policy = _access_groups.find(group);
    if (group_policy == _access_groups.end())
    {
        if (reason)
        {
            *reason = "Access group not found";
        }
        return false;
    }
    if (!group_policy->second.nearest_mpt_enable())
    {
        if (reason)
        {
            *reason = "Nearest mount point disabled by group policy";
        }
        return false;
    }
    if (group_policy->second.nearest_mpt_source_name() != mount_point)
    {
        if (reason)
        {
            *reason = "Nearest mount point not configured for group";
        }
        return false;
    }

    return true;
}

bool AccessPolicyService::check_mount_visible(const std::string &group_uid,
                                              const std::string &mount_point,
                                              std::string *reason) const
{
    if (_access_groups.empty() && _access_items.empty())
    {
        return true;
    }

    const auto group = normalize_access_group_uid(group_uid);
    if (is_privileged_access_group(group))
    {
        return true;
    }

    auto group_policy = _access_groups.find(group);
    if (group_policy == _access_groups.end())
    {
        if (reason)
        {
            *reason = "Access group not found";
        }
        return false;
    }

    auto group_items = _access_items.find(group);
    if (group_items != _access_items.end())
    {
        auto item = group_items->second.find(mount_point);
        if (item != group_items->second.end())
        {
            if (item->second.allow_visible() == caster::core::ACCESS_STATE_ENABLE)
            {
                return true;
            }
            if (item->second.allow_visible() == caster::core::ACCESS_STATE_DISABLE)
            {
                if (reason)
                {
                    *reason = "Mount point visible disabled by item policy";
                }
                return false;
            }
        }
    }

    const bool inside_group = is_mount_inside_group(group, mount_point);
    const bool allowed = inside_group ? group_policy->second.allow_visible_inside_group()
                                      : group_policy->second.allow_visible_outside_group();
    if (!allowed && reason)
    {
        *reason = inside_group ? "Mount point visible disabled inside group"
                               : "Mount point visible disabled outside group";
    }
    return allowed;
}

bool AccessPolicyService::check_mount_access(const std::string &group_uid,
                                             const std::string &mount_point,
                                             std::string *reason) const
{
    if (_access_groups.empty() && _access_items.empty())
    {
        return true;
    }

    const auto group = normalize_access_group_uid(group_uid);
    if (is_privileged_access_group(group))
    {
        return true;
    }

    auto group_policy = _access_groups.find(group);
    if (group_policy == _access_groups.end())
    {
        if (reason)
        {
            *reason = "Access group not found";
        }
        return false;
    }
    auto group_items = _access_items.find(group);
    if (group_items != _access_items.end())
    {
        auto item = group_items->second.find(mount_point);
        if (item != group_items->second.end())
        {
            if (item->second.allow_access() == caster::core::ACCESS_STATE_ENABLE)
            {
                return true;
            }
            if (item->second.allow_access() == caster::core::ACCESS_STATE_DISABLE)
            {
                if (reason)
                {
                    *reason = "Mount point access disabled by item policy";
                }
                return false;
            }
        }
    }

    const bool inside_group = is_mount_inside_group(group, mount_point);
    const bool allowed = inside_group ? group_policy->second.allow_access_inside_group()
                                      : group_policy->second.allow_access_outside_group();
    if (!allowed && reason)
    {
        *reason = inside_group ? "Mount point access disabled inside group"
                               : "Mount point access disabled outside group";
    }
    return allowed;
}

bool AccessPolicyService::check_mount_nearby(const std::string &group_uid,
                                             const std::string &mount_point,
                                             std::string *reason) const
{
    if (_access_groups.empty() && _access_items.empty())
    {
        return true;
    }

    const auto group = normalize_access_group_uid(group_uid);
    if (is_privileged_access_group(group))
    {
        return true;
    }

    auto group_policy = _access_groups.find(group);
    if (group_policy == _access_groups.end())
    {
        if (reason)
        {
            *reason = "Access group not found";
        }
        return false;
    }
    if (!group_policy->second.nearest_mpt_enable())
    {
        if (reason)
        {
            *reason = "Nearest mount point disabled by group policy";
        }
        return false;
    }

    auto group_items = _access_items.find(group);
    if (group_items != _access_items.end())
    {
        auto item = group_items->second.find(mount_point);
        if (item != group_items->second.end())
        {
            if (item->second.allow_nearby() == caster::core::ACCESS_STATE_ENABLE)
            {
                return true;
            }
            if (item->second.allow_nearby() == caster::core::ACCESS_STATE_DISABLE)
            {
                if (reason)
                {
                    *reason = "Mount point nearby disabled by item policy";
                }
                return false;
            }
        }
    }

    const bool inside_group = is_mount_inside_group(group, mount_point);
    const bool allowed = inside_group ? group_policy->second.allow_nearby_inside_group()
                                      : group_policy->second.allow_nearby_outside_group();
    if (!allowed && reason)
    {
        *reason = inside_group ? "Mount point nearby disabled inside group"
                               : "Mount point nearby disabled outside group";
    }
    return allowed;
}
} // namespace navcaster::core
