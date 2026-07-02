#pragma once

#include "controller_response.h"

#include <functional>
#include <string>
#include <vector>

namespace navcaster::http_api
{

using RingLogReader = std::function<std::vector<std::string>(std::size_t)>;

int infer_ring_log_level(const std::string &line);
std::size_t normalize_ring_log_count(std::size_t count);

class RingLogService
{
public:
    explicit RingLogService(RingLogReader reader);

    ControllerResponse list(std::size_t count, const std::string &level, long long timestamp_ms);

private:
    RingLogReader _reader;
};

} // namespace navcaster::http_api
