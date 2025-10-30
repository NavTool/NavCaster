#include "client_grid.h"

client_grid::client_grid(json req, bufferevent *bev)
{
}

client_grid::~client_grid()
{
}

int client_grid::start()
{

    //如果已经提供GPGGA，那么执行一次更新格网点操作

    //查询最近的虚拟格网点

    //订阅最近的虚拟格网点




    //如果没有提供GPGGA,那么直接回应ICY_200_OK



    return 0;
}

int client_grid::stop()
{
    // 取消注册所有的格网点

    // 取消注册


    return 0;
}

int client_grid::runing()
{
    return 0;
}

int client_grid::bev_send_reply()
{
    return 0;
}

int client_grid::transfer_sub_raw_data(const char *data, size_t length)
{
    return 0;
}

int client_grid::publish_recv_raw_data()
{
    return 0;
}

void client_grid::ReadCallback(bufferevent *bev, void *arg)
{

    //解析GGA


    //判断距离与当前基站是否超限


    //如果超限
        //触发一次查询工作，查询最近的在线基站List


    //如果超限则重新订阅新的基站（需要取消订阅吗？）

    //订阅指定范围的最近基站，返回订阅基站的信息


    //取消订阅最近的基站，反复的取消订阅，订阅最新的基站，重复流程



}

void client_grid::EventCallback(bufferevent *bev, short events, void *arg)
{


    //触发 stop操作

}

void client_grid::Auth_Login_Callback(const char *request, void *arg, AuthReply *reply)
{
}

void client_grid::Caster_Register_Callback(const char *request, void *arg, catser_reply *reply)
{

    //注册到Caster节点中，Caster会查询这个挂载点是否合法，如果合法就把数据组装规则告诉Client，如果不合法，则直接返回错误回调
    //这么设计的前提：对于一般的用户来说，一般都是连接一个已知的挂载点，而不是连接一个不存在的挂载点
    //              因此就逻辑上来说，是一个乐观估计，即：大部分情况下，返回的应当是合法的，只有少部分不合法

    


}

void client_grid::Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply)
{

    //如果返回的是OK，则表示订阅成功（返回值中应包含虚拟站点的坐标，用于触发站点更新机制）

    //如果返回的是数据，则直接发送数据

    //如果返回的是订阅的站点已经离线，则重新激活订阅机制

    //如果返回的是没有可用基站，则服务不可用，关闭连接

}
