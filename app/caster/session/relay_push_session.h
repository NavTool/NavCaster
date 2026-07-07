#pragma once

#include <utility>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class RelayPushSession {
public:
    explicit RelayPushSession(HandoffMessage handoff) : _handoff(std::move(handoff)) {}

private:
    HandoffMessage _handoff;
};

} // namespace navcaster::caster
