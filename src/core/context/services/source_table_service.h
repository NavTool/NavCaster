#pragma once

#include "access_policy_service.h"

#include <string>
#include <unordered_map>

namespace navcaster::core
{
using AliasVisibleMap = std::unordered_map<std::string, std::string>;

class SourceTableService
{
public:
    SourceTableService(const SourceRecordMap &source_records,
                       const SourceRecordMap &source_decodes,
                       const AccessGroupMap &access_groups,
                       const AccessItemMap &access_items,
                       const AliasVisibleMap &visible_aliases);

    std::string build_text(const std::string &group_uid) const;

private:
    SourceRecordMap merged_sources() const;

    const SourceRecordMap &_source_records;
    const SourceRecordMap &_source_decodes;
    const AccessGroupMap &_access_groups;
    const AccessItemMap &_access_items;
    const AliasVisibleMap &_visible_aliases;
};
} // namespace navcaster::core
