#pragma once

#include <utility>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class RelayPullSession {
public:
    explicit RelayPullSession(HandoffMessage handoff) : _handoff(std::move(handoff)) {}

private:
    HandoffMessage _handoff;
};

} // namespace navcaster::caster
