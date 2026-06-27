#pragma once

#include <utility>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class RelayPushSession {
public:
    explicit RelayPushSession(HandoffMessage handoff) : handoff_(std::move(handoff)) {}

private:
    HandoffMessage handoff_;
};

} // namespace navcaster::caster
