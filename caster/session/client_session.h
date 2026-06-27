#pragma once

#include <utility>

#include "transport/handoff_message.h"

namespace navcaster::caster {

class ClientSession {
public:
    explicit ClientSession(HandoffMessage handoff) : handoff_(std::move(handoff)) {}

private:
    HandoffMessage handoff_;
};

} // namespace navcaster::caster
