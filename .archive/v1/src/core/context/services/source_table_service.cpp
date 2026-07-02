#include "source_table_service.h"

#include "context_util.h"

#include <set>

namespace navcaster::core
{
SourceTableService::SourceTableService(const SourceRecordMap &source_records,
                                       const SourceRecordMap &source_decodes,
                                       const AccessGroupMap &access_groups,
                                       const AccessItemMap &access_items,
                                       const AliasVisibleMap &visible_aliases)
    : _source_records(source_records),
      _source_decodes(source_decodes),
      _access_groups(access_groups),
      _access_items(access_items),
      _visible_aliases(visible_aliases)
{
}

SourceRecordMap SourceTableService::merged_sources() const
{
    SourceRecordMap merged = _source_decodes;
    for (const auto &item : _source_records)
    {
        merged.insert_or_assign(item.first, item.second);
    }
    return merged;
}

std::string SourceTableService::build_text(const std::string &group_uid) const
{
    const auto group = normalize_access_group_uid(group_uid);
    const auto merged = merged_sources();
    AccessPolicyService access_policy(_source_records, _source_decodes, _access_groups, _access_items);

    std::string text;
    std::set<std::string> emitted_mounts;
    for (const auto &item : merged)
    {
        if (!access_policy.check_mount_visible(group, item.first))
        {
            continue;
        }
        text += item.second.toSourceItem();
        emitted_mounts.insert(item.second.mountpoint());
    }

    for (const auto &alias : _visible_aliases)
    {
        const std::string &alias_name = alias.first;
        const std::string &source_name = alias.second;

        if (emitted_mounts.find(alias_name) != emitted_mounts.end())
        {
            continue;
        }
        if (!access_policy.check_mount_visible(group, alias_name))
        {
            continue;
        }

        auto source = merged.find(source_name);
        if (source == merged.end())
        {
            continue;
        }

        source_record alias_record = source->second;
        alias_record.set_mountpoint(alias_name);
        text += alias_record.toSourceItem();
        emitted_mounts.insert(alias_name);
    }

    auto group_policy = _access_groups.find(group);
    if (group_policy != _access_groups.end() && group_policy->second.nearest_mpt_enable())
    {
        const auto &nearest_mount = group_policy->second.nearest_mpt_source_name();
        if (nearest_mount.empty() || emitted_mounts.find(nearest_mount) != emitted_mounts.end())
        {
            return text;
        }

        auto nearest_info = build_default_mount_info(nearest_mount);
        text += convert_mount_info_to_string(nearest_info);
        emitted_mounts.insert(nearest_mount);
    }

    return text;
}
} // namespace navcaster::core
