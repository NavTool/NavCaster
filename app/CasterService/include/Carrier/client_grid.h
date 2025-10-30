///用来实现NEAREST RELAY VIRTUAL 功能



//通用逻辑 

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

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

class client_grid
{
private:
    json _info;
    std::string _connect_key;
    std::string _mount_point;
    std::string _user_name;
    std::string _ip;
    int _port;

    int _connect_timeout = 0;
    int _unsend_byte_limit;

    bool _NtripVersion2 = false;
    bool _transfer_with_chunked = false;

    json _conf;

    bufferevent *_bev;
    timeval _bev_read_timeout_tv;

    evbuffer *_send_evbuf;
    evbuffer *_recv_evbuf;

public:
    client_grid(json req, bufferevent *bev);
    ~client_grid();

    int start(); // 绑定回调，然后去AUTH添加登录记录（是否允许多用户登录由auth判断并处理），如果添加成功，那就发送reply给用户，然后通知CASTER上线，如果不成功，就进入关闭流程
    int stop();

private:
    int runing();

    int bev_send_reply();
    int transfer_sub_raw_data(const char *data, size_t length);
    int publish_recv_raw_data();

    static void ReadCallback(struct bufferevent *bev, void *arg);
    static void EventCallback(struct bufferevent *bev, short events, void *arg);

    static void Auth_Login_Callback(const char *request, void *arg, AuthReply *reply);
    static void Caster_Register_Callback(const char *request, void *arg, catser_reply *reply);
    static void Caster_Sub_Callback(const char *request, void *arg, catser_reply *reply);

private:

    int decode_GGA();


    //添加新的订阅条件

    int Geo_Search_Callback(const char *request, void *arg, catser_reply *reply); //查询指定半径挂载点
    int Grid_Search_Callback(const char *request, void *arg, catser_reply *reply); //获取指定半径挂载点是否在线



};
