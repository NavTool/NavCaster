#pragma once

#include <event2/util.h>
#include <event2/event.h>
#include <event2/http.h>

#include <queue>
#include <list>
#include <mutex>
#include <memory>

#include <spdlog/spdlog.h>

#include "service/info_connect.pb.h"
using namespace caster::service;

namespace QUEUE
{
    int Init(event *process_event);
    int Free();

    int Push(ConnectInfo req);
    ConnectInfo Pop();

    bool Active();
    bool Not_Null();

} // namespace QUEUE
