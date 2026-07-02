#pragma once

#include <string>

namespace navcaster::http_api
{

struct ControllerResponse
{
    int status_code = 200;
    std::string body;
};

} // namespace navcaster::http_api
