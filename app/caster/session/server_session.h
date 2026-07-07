#pragma once

#include <utility>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class ServerSession {
public:
    explicit ServerSession(HandoffMessage handoff) : _handoff(std::move(handoff)) {}

private:
    HandoffMessage _handoff;
};

} // namespace navcaster::caster
