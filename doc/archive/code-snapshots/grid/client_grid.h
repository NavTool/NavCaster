// Archived documentation snapshot. Not current product source; see doc/archive/code-snapshots/README.md.
/// 用来实现NEAREST RELAY VIRTUAL 功能

// 通用逻辑

/*
    通用逻辑
    NEAREST和VIRTUAL都要解析GGA、要根据位置选择最近站点
        NEAREST，选择最近的实体挂载点
                1、解析GGA，获取变动值，不超限，就不触发新筛选
                2、筛选指定半径内在线的实体挂载点
                3、订阅指定挂载点
        VIRTUAL，选择最近的虚拟挂载点，同时还要通知挂载点推送数据和停止推送数据
                1、解析GGA，获取变动值，不超限，就不触发新筛选
                2、筛选指定半径内在线的虚拟挂载点
                3、订阅指定挂载点
                4、通知生成挂载点数据
    RELAY，不用解析GGA，直接转发GGA

*/

/*
    虚拟站点挂载逻辑：

    发送的消息中包含GGA，
        触发订阅逻辑

    发送的消息中不包含GGA，
        等待GGA来，触发订阅逻辑

    用户名验证成功就可以发送ICY 200 OK了，看是Ntrip1.0还是2.0





*/

/*
    用户已经上线的情况下，向redis写入用户登录信息
    判断当前已登录的用户数量，如果超过限制，启动下线流程
*/
#pragma once
#include "ntrip_global.h"
#include "process_queue.h"
#include "carrier_base.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// class client_grid : public carrier_base
// {
// public:
//     client_grid(ConnectInfo info);
//     ~client_grid();

// };
