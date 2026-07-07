#pragma once

#include <utility>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class NearSession {
public:
    explicit NearSession(HandoffMessage handoff) : _handoff(std::move(handoff)) {}

private:
    HandoffMessage _handoff;
};

} // namespace navcaster::caster
