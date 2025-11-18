#include "Caster_Core.h"

#include "Caster_Core.h"
#include <string>
#include <unordered_map>
#include <set>

#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

/*
    库内维护的Redis表和结构说明

    基本定义：
    挂载点：实体的接收机基站，发送真实的数据流

    参考站：生成格网点的数据源，参考站数据并不播发给实际用户
    格网点：虚拟的基站，由参考站数据推理生成

    用户：实体的用户，接受数据流数据



    CASTER_CORE配置文件
    Caster_Setting:
        Update_Intv: 1          #内部数据更新频率
        Unactive_Time: 5        #非活动连接判断间隔

    Base_Setting:
        Enable_Mult: false      #允许多连接采用同一挂载点
        Keep_Early: false       #保持较早登录的连接(踢掉同名的新连接)(Enable_Mult为false才生效）

    Rover_Setting:
        Enable_Mult: true       #允许多连接采用同一挂载点
        Keep_Early: false       #保持较早登录的连接(踢掉同名的新连接)(Enable_Mult为false才生效）

    Grid_Setting:
        Use_Coord: WGS84        #使用的坐标框架

    Source_Setting:
        Show_Base:             #挂载点列表显示实体基站
        Show_Grid:             #挂载点列表显示Grid
        Show_Nearest:          #挂载点列表显示最近基站

    # Redis连接
    Reids_Connect_Setting:
        IP: 127.0.0.1           #如果是docker-compse启动  IP直接设置为redis
        Port: 6379
        Requirepass: password


    MPT:REC:KORO5       类型HASH,  挂载点名-[基站的ConnectKey-添加记录的时间]     记录单个挂载点的详细信息（发布者列表，一般来说发布者只允许有一个）
    MPT:SUB:KORO5       类型Hash， 挂载点名-[用户的ConnectKey-添加记录的时间]     记录单个挂载点的订阅情况（订阅者列表，订阅者数量没有限制）
    MPT:LIST            类型Hash，         [挂载点名-数据流的挂载点信息]            记录当前在线挂载点的情况，（如果程序不挂掉，挂载点的维护用不到UTCtime，UTCtime就是为了验证在线的有效性，并且这个Hash可以一次性查询所有在线挂载点）  
    MPT:GEO             类型GEO，          [挂载点名-经度-纬度]                    解析出来的单个挂载点的位置信息
    MPT:STAT            类型HASH，         [基站的ConnectKey-数据流详细信息]      设置对应挂载点的RTCM数据组装规则，Client注册到Catser中的时候，Caster会查询规则，同时把规则返回给Client，这样Client就可以知道要如何组装数据了

    USR:REC:KORO5       类型HASH,  用户名 -[用户的ConnectKey-添加记录的时间]    记录单个用户的在线情况（对于用户来说，可以允许多个用户在线）
    USR:SUB:KORO5       类型Hash， 用户名 -[基站的ConnectKey-添加记录的时间]    记录单个用户的订阅情况（订阅者列表，订阅者数量没有限制）
    USR:LIST            类型Hash，         [用户名-XXXXX]              
    USR:GEO             类型GEO，          [用户名-经度-纬度]           
    USR:STAT            类型HASH，         [用户的ConnectKey-数据流详细信息]      


    这个设计结构是兼容设计模式，可以支持多用户和多基站同时在线，但是只是兼容多用户，所以对于多用户来说，并不是特别合理，譬如如果多个基站都在线，那么订阅这个基站的人会收到所有发布基站的数据，订阅用户频道的话，所有的在线用户发送的消息也都会被订阅者收到
    其实最优的模式应当是直接订阅到ConnectKey？


    GRID:REC:KORO5      类型HASH,  挂载点名-[发布者的ConnectKey-UTCtime]    记录单个挂载点的在线情况（发布者列表，格网模式的发布者只允许有一个，允许多个但是实际不会多个同时工作，只是为了保证分布式节点的同步）
    GRID:SUB:KORO5      类型Hash， 挂载点名-[订阅者的ConnectKey-UTCtime]    记录单个挂载点的订阅情况（订阅者列表，订阅者数量没有限制）
    GRID:LIST           类型Hash，    [挂载点名-UTCtime]                记录当前在线格网点的情况，（如果程序不挂掉，挂载点的维护用不到UTCtime，UTCtime就是为了验证在线的有效性，并且这个Hash可以一次性查询所有在线挂载点）
    GRID:REF:LIST       类型Hash，    [挂载点名-UTCtime]                记录当前在线参考站的情况，（如果程序不挂掉，挂载点的维护用不到UTCtime，UTCtime就是为了验证在线的有效性，并且这个Hash可以一次性查询所有在线挂载点）


    GRID:INFO:GEO       类型GEO    [挂载点名-经度-纬度]  人工设置的单个格网点的位置信息
    GRID:INFO:STR       人工设置的挂载点信息，即每个挂载点
    GRID:INFO:CTRL      [挂载点名-挂载点输出 控制信息] 设置对应挂载点的RTCM数据组装规则，Client注册到Catser中的时候，Caster会查询规则，同时把规则返回给Client，这样Client就可以知道要如何组装数据了
    GR    OOXX
    GREC  0000
    GRE   OOOX
    C     XXX0

    配置页面：
    使用的格网表：
    GRID:COORD:WGS84    类型HASH    [格网点名-RTCM1005]      WGS84框架下的各个格网点的坐标
    GRID:COORD:CGCS     类型HASH    [格网点名-RTCM1005]      CGCS框架下的各个格网点的坐标
    GRID:COORD:ITRF     类型HASH    [格网点名-RTCM1005]      ITRF框架下的各个格网点的坐标

    格网点坐标/框架1/框架2/框架3  1005 1005 1005

    //通讯频道：
    MPT:KORO5    基站数据频道
    USR:KORO5    用户数据频道
    GRID:KORO5   格网数据频道

    //广播频道：
    CASTER:BROADCAST   //用于所有频道的同步广播，所有节点的公共频道（Caster_Core之间的同步频道）


    //在线挂载点同步机制


*/

/*
    基站类型：
        1.  对于一般的基站  登录的时候，设置login_mpt为实际的挂载点名称(SHJD01)，alias_mpt也设置为实际的挂载点名称(SHJD01)
        2.  对于最近挂载点(这个只是存在与配置文件中的挂载点列表中，并不是真实存在的挂载点） 
        3.  对于Relay挂载点（Ntrip Client）login_mpt设置为接入第三方的挂载点名称(SSRA03IGS0_SIRGAS2000)，alias_mpt设置为本Caster对外服务的挂载点名称(IGS_SP3)
        4.  对于Relay挂载点（TCP Client） login_mpt设置为接入第三方的备注名(CH01)，alias_mpt设置为本Caster对外服务的挂载点名称(CH01)
        5.  对于Relay挂载点（TCP Server） login_mpt设置为本地TCP端口的备注名(SN01_10009)，alias_mpt设置为本Caster对外服务的挂载点名称(SN01)
        6.  对于Proxy挂载点  login_mpt设置为第三方的挂载点名称(CMCC-GRECJ)，alias_mpt也设置Proxy+触发代理用户的识别ID(SN-GRECJ_0F64) 为挂载点名称
        7.  对于Alias挂载点  login_mpt设置为代理的挂载点名称(SHJD)，alias_mpt设置为别名挂载点名称(SHJD01)

    用户类型：
        1.  对于一般的用户   登录的时候，设置login_mpt为实际订阅的挂载点名称(SHJD01)，alias_mpt也设置为实际订阅的挂载点的alias_mpt名称(SHJD01)
        2.  对于最近基站用户 登录的时候，设置login_mpt为提供的Nearest挂载点名称（NEAREST)，alias_mpt设置为实际提供服务的基站alias_mpt(SHJD01)名称
        3.  对于Proxy用户   登录的时候，设置login_mpt为提供的Proxy挂载点名称（SN-GRECJ），alias_mpt设置为Proxy挂载点的(SN-GRECJ_0F64)名称
*/

class str_status
{
private:
    std::string _UID;       // TCP连接唯一标识
    std::string _login_mpt; // 用户接入的挂载点 | 基站接入使用的挂载点 | 接入的第三方挂载点名称
    std::string _alias_mpt; // 挂载点对外服务的名称
    int _type;               /* 挂载点类型
                             *  0：未知
                             *  1：普通挂载点
                             *  2：最近挂载点
                             *  3：Relay挂载点（Ntrip Client）
                             *  4：Relay挂载点（TCP Client）
                             *  5：Relay挂载点（TCP Server）
                             *  6：Proxy挂载点（Ntrip Client）
                             *  7：Alias挂载点（挂载点添加一个别名，可通过这个别名来获取数据）
                             */
                            /* 接入类型
                             *  0：未知
                             *  1：普通接入模式
                             *  2：最近基站模式
                             *  3：Proxy模式
                             */

    std::string _account;
    std::string _ip;
    int _port = 0;
    std::time_t _online_time = 0;    // 上线时刻
    std::time_t _online_seconds = 0; // 上线持续时间

    size_t _send_total = 0;   // 总发送字节数
    double _send_speed = 0.0; // 总发送速度
    size_t _recv_total = 0;   // 总接收字节数
    double _recv_speed = 0.0; // 总接收速度

    double _ecef_x = 0.0;
    double _ecef_y = 0.0;
    double _ecef_z = 0.0;
    long long _position_update_time = 0; // 坐标更新时间戳 如果和信息更新时刻相隔太远，就认为坐标无效

    std::time_t _update_time = 0.0;
    ; // 信息更新时刻（执行所有函数的时候，都会更新一下这个函数）

    size_t _send_count = 0; // 用于统计最近发送速度（而不是整体发送速度）(每隔5秒更新一次速度）)
    size_t _recv_count = 0; // 用于统计最近接收速度（而不是整体接收速度）(每隔5秒更新一次速度）)

public:
    str_status(std::string login_mpt,std::string alias_mpt ,int type, std::string user_name, std::string connect_key);
    ~str_status();

    int add_recv(int size);
    int add_send(int size);

    int set_alias_mpt(std::string alias_mpt);

    int set_coord_info(double ecef_x, double ecef_y, double ecef_z, long long update_time);

    int update_speed();

    std::string get_status_str();
};

class caster_cb_item
{
public:
    std::string connect_key;
    std::string channel;
    std::string user_name;
    CasterCallback cb;
    void *arg;
};

class caster_internal
{
private:
    std::string _redis_IP;
    int _redis_port;
    std::string _redis_Requirepass;

    event_base *_base;

    event *_timeout_ev;
    timeval _timeout_tv;

    // conf
    int _unactive_time = 10; // 站点更新时间和当前时间差距多少秒会被认为已挂掉
    int _update_intv = 1;
    int _key_expire_time = 3600; // Hash键值默认续期时间

    bool _upload_base_stat = true;     // 上报基站数据流统计信息
    bool _upload_rover_stat = true;    // 上报用户数据流统计信息
    bool _download_base_stat = false;  // 下载基站数据流统计信息
    bool _download_rover_stat = false; // 下载用户数据流统计信息

    bool _base_enable_mult = false; // 允许多个同名基站同时在线
    bool _base_keep_early = false;  // 不允许后续同名基站上线（_base_enable_mult=false的时候才生效）

    bool _rover_enable_mult = true; // 允许多个同名用户同时在线
    bool _rover_keep_early = false; // 不允许后续同名用户上线（_rover_enable_mult=false的时候才生效）

    bool _notify_base_inactive = true;  // 当基站不在线的时候，通知所有订阅该基站的连接
    bool _notify_rover_inactive = true; // 当用户不在线的时候，通知所有订阅该用户的连接

private:
    // 本地记录  <挂载点|用户名>/<Connect_Key>/<回调参数>
    // 频道名（挂载点，用户名）：[具体连接key:连接回调]
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _base_sub_map;       // channel/connect_key/cb_arg
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _rover_sub_map;      // channel/connect_key/cb_arg
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _base_register_map;  // channel/connect_key/cb_arg
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _rover_register_map; // channel/connect_key/cb_arg

    std::unordered_map<std::string, str_status> _base_status_map;  // connect_key/str_status  //基站的状态统计信息
    std::unordered_map<std::string, str_status> _rover_status_map; // connect_key/str_status  //移动站的状态统计信息

    std::unordered_map<std::string, mount_info> _mount_map; // Mount_Point // 挂载点名为XXXX-F1A6(虚拟挂载点名-本地连接第三方时采用的端口转为4位16进制)
    std::set<std::string> _active_mount_set;                // 在线挂载点
    std::set<std::string> _active_user_set;                 // 在线用户名

    std::set<std::string> _active_baseUID_set;                           // 在线基站连接
    std::set<std::string> _active_roverUID_set;                          // 在线用户连接
    std::unordered_map<std::string, std::string> _active_base_info_map;  // 在线基站连接信息
    std::unordered_map<std::string, std::string> _active_rover_info_map; // 在线用户连接信息

    std::string _source_list_text;

    std::string _updatetime_str;
    long long _updatetime_int;

    std::set<std::string> _sync_real_set;                                                                // 多节点同步在线的实体站
    std::set<std::string> _sync_grid_set;                                                                // 多节点同步在线格网点
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _grid_sub_map;      // channel/connect_key/cb_arg   本地格网点订阅
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _grid_register_map; // channel/connect_key/cb_arg   本地格网点注册

public:
    bool _is_pub_connected = false;
    bool _is_sub_connected = false;
    int _pub_reconnect_count = 0; // 重连计数  连接成功后归零   重连失败后，等待时间0、2、4、8、10（max）
    int _sub_reconnect_count = 0; // 重连计数
    std::string _pub_context_errstr;
    std::string _sub_context_errstr;
    redisAsyncContext *_pub_context = nullptr;
    redisAsyncContext *_sub_context = nullptr;

public:
    caster_internal(json conf, event_base *base);
    ~caster_internal();

    int start();
    int stop();

    std::string get_status_str();

    // 注册基站频道 MPT:XXXXXX
    int register_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 注销频道
    int withdraw_base_channel(const char *channel, const char *user_name, const char *connect_key);
    // 向注册的基站频道发送状态消息
    int send_status_base_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason);
    // 向频道发布数据
    int pub_base_channel(const char *mount_point, const char *connect_key, const char *data, size_t data_length);
    // 订阅指定频道
    int sub_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅频道
    int unsub_base_channel(const char *channel, const char *connect_key);

    // 注册移动站频道 USR:XXXXXX
    int register_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 注销频道
    int withdraw_rover_channel(const char *channel, const char *user_name, const char *connect_key);
    // 向注册的移动站频道发送状态消息
    int send_status_rover_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason);
    // 向频道发布数据
    int pub_rover_channel(const char *user_name, const char *connect_key, const char *data, size_t data_length);
    // 订阅指定频道
    int sub_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅频道
    int unsub_rover_channel(const char *channel, const char *connect_key);

    // 获取挂载点列表正文
    std::string get_source_list_text();

    int set_base_coord_info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, long long update_time);

    std::set<std::string> get_active_base_UID();
    std::set<std::string> get_active_rover_UID();
    std::string get_active_base_info(std::string UID);
    std::string get_active_rover_info(std::string UID);

private:
    static long long get_time_stamp();

    int clear_overdue_item(); // 清理为空的注册记录

    int upload_record_item();   // 将本地记录的所有连接、挂载点和用户更新到redis中（更新记录时间）
    int download_active_item(); // 将云端记录的在线挂载点更新到本地

    int download_active_info(); // 将云端记录的基站和用户信息更新到本地（只有monitor才需要更新这个信息）

    int check_active_base_channel();  // 检测活跃基站频道（如果已经不存在，那么就踢出本地连接）
    int check_active_rover_channel(); // 检测活跃基站频道（如果已经不存在，那么就踢出本地连接）

    int build_source_list();

    std::string convert_mount_info_to_string(mount_info item);
    mount_info build_default_mount_info(std::string mount_point);

    // libevent 回调
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    // 注册回调
    static void Redis_Register_Base_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_Register_Rover_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 订阅一般频道的回调
    static void Redis_SUB_Base_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_SUB_Rover_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 广播频道的回调
    static void Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 更新有效挂载点、有效用户的回调
    static void Redis_Update_Active_Base_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_Update_Active_Rover_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 更新挂载点、用户的详细信息回调（Monitor）
    static void Redis_Update_Base_Info_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_Update_Rover_Info_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 查询回调 (传入的privdata 类型 std::unordered_map<std::string, std::string> *
    static void Redis_Get_Hash_Field_Callback(redisAsyncContext *c, void *r, void *privdata);
    // 查询回调 (传入的privdata 类型 std::set<std::string> *
    static void Redis_Get_Set_Value_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 异常处理机制：
    // redis断开连接
    // 如果是pub发生连接断开

    // 如果是sub发生连接断开

    static void Redis_Pub_ReconnectCallback(evutil_socket_t fd, short events, void *arg);
    static void Redis_Sub_ReconnectCallback(evutil_socket_t fd, short events, void *arg);

    // redis回调
    static void Redis_Connect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Disconnect_Cb(const redisAsyncContext *c, int status);

    static void Redis_Pub_Connect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Sub_Connect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Pub_Disconnect_Cb(const redisAsyncContext *c, int status);
    static void Redis_Sub_Disconnect_Cb(const redisAsyncContext *c, int status);

    int init_sub_context();
    int init_pub_context();

    int subAttemptReconnect();
    int pubAttemptReconnect();
};
