#include "domain/connect_info.h"

namespace navcaster::caster {

std::string connect_type_name(ConnectType type)
{
    switch (type) {
    case ConnectType::SourceTable:
        return "source_table";
    case ConnectType::Source:
        return "source";
    case ConnectType::Client:
        return "client";
    case ConnectType::Near:
        return "near";
    case ConnectType::RelayPull:
        return "relay_pull";
    case ConnectType::RelayPush:
        return "relay_push";
    case ConnectType::Unknown:
    default:
        return "unknown";
    }
}

} // namespace navcaster::caster
