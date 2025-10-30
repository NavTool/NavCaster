#pragma once
#include "ntrip_global.h"
#include "process_queue.h"

#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class server_grid
{
private:
    /* data */

    // 站点基本信息
    std::string unit_name; // 站点名称

    // 站点坐标

    // 订阅的VRS频道

    // 订阅的Station频道

    // 维护的虚拟站点信息
    // 点位名称-点位坐标

    // 格网点覆盖范围可以重复，格网点命名规则中包含距离信息，则在进行最近点筛选的时候，可以根据距离实现筛选
    std::list<std::string> _gen_grid;    // 所有可生成的格网节点
    std::list<std::string> _hold_grid;   // 本地维护的格网节点（经过判断由该站点维护的格网点）
    std::list<std::string> _active_grid; // 已经激活的格网节点,节点名，Connect_Key

    std::list<std::string> _radius_station; // 半径内的所有基站，和在线状态 （坐标，在线状态/可用状态）

public:
    server_grid(/* args */);
    ~server_grid();

    int start(); //启动服务，监听指定的VRS_Data
    int stop();

private:
    static void VRS_Data_Callback(); // 注册函数，收到数据更新VRS数据
    static void VRS_Timeout_Callback(evutil_socket_t fd, short events, void *arg);

    static void Base_Data_Callback(); // 注册函数，收到数据触发虚拟节点生成和数据推送逻辑
    static void Base_Timeout_Callback(evutil_socket_t fd, short events, void *arg);

    // 将虚拟挂载点注册到Caster_Core
    static void Caster_Register_Callback(const char *request, void *arg, void *reply);

    static void Timeout_Callback(evutil_socket_t fd, short events, void *arg);

private:
    // 生成格网点（根据实体站坐标生成格网点经纬度：_gen_grid）
    int generate_grid_point();

    // 根据在线基站更新持有的格网点（定时任务）
    int update_hold_point(); 

    // 生成数据
    int generate_grid_data();
    // 推送激活节点数据(全量推送)
    int publish_grid_data();

    // 注册格网点到Caster, 注册到Caster库中（添加在线表记录），同时更新GEO坐标
    int register_grid_point();
    // 取消注册格网点（除了对象被删除，否则一般不会主动调用），即使不激活，也不主动取消注册，通常是register_grid_point()回调告知这个连接被踢出，然后再取消注册
    int withdraw_grid_point();
    //
};
