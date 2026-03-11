#include "server_grid.h"

server_grid::server_grid()
{
}

server_grid::~server_grid()
{
}

int server_grid::start()
{
//初始化任务------------------------------------------------

    // 筛选点位指定半径（2倍格网生成半径）的实体站点（Redis一次查询）

    // 根据配置信息（点位坐标，格网生成参数，得到所有可生成格网点）

    // 根据半径内的实体站点和格网点坐标，

    // 计算各个实体站点对于每个格网点的优先级

    // 将这个优先级存储到本地List中

//启动任务------------------------------------------------

    //绑定回调，接受BaseStation和VRS频道数据

//定时任务------------------------------------------------

    // 查询所有实体站点的在线状态（所有实体站点的在线状态默认设置为false）

    // 根据在线状态，是否改变，来刷新实体格网点（hold_grid)

    // 实体格网点改变（非激活->激活），则注册到Caster中

    return 0;
}

int server_grid::stop()
{
    return 0;
}

void server_grid::VRS_Data_Callback()
{
}

void server_grid::VRS_Timeout_Callback(evutil_socket_t fd, short events, void *arg)
{
}

void server_grid::Base_Data_Callback()
{
}

void server_grid::Base_Timeout_Callback(evutil_socket_t fd, short events, void *arg)
{
}

void server_grid::Caster_Register_Callback(const char *request, void *arg, void *reply)
{
}

void server_grid::Timeout_Callback(evutil_socket_t fd, short events, void *arg)
{
}

int server_grid::generate_grid_point()
{



    //格网点的connect_key生成规则
    //实体站名称-虚拟站点名称（经纬度）

    return 0;
}

int server_grid::update_hold_point()
{

    //查询_radius_station中的基站的在线状态是否有更改

    //没有更改，不用更新

    //发现更改，更新需要生成的挂载点

        //将新持有的挂载点，注册到Caster_Core中，（Caster_Core一般会注册成功),
            //成功，则添加到hold中，
            //失败，则再次计算是否需要注册，不需要那就不注册了



    return 0;
}

int server_grid::generate_grid_data()
{
    return 0;
}

int server_grid::publish_grid_data()
{
    return 0;
}

int server_grid::register_grid_point()
{
    return 0;
}

int server_grid::withdraw_grid_point()
{
    return 0;
}
