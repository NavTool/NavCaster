#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "domain/position.h"

namespace navcaster::caster {

class Rtcm3Parser {
public:
    std::vector<PositionReport> feed(const char *data, std::size_t length);
    std::vector<PositionReport> feed(const std::string &data) { return feed(data.data(), data.size()); }
    void reset();

private:
    std::vector<std::uint8_t> _buffer;
};

} // namespace navcaster::caster
