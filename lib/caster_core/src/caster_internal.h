#include "Caster_Core.h"

#include "Caster_Core.h"
#include "knt.h"
#include <string>
#include <unordered_map>
#include <set>
#include <deque>
#include <hiredis.h>
#include <async.h>
#include <adapters/libevent.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "access_group.h"
#include "access_item.h"
#include "alias_rule.h"
#include "boardcast_msg.h"
#include "caster_node.h"
#include "client_status.h"
#include "pull_record.h"
#include "pull_status.h"
#include "push_record.h"
#include "push_status.h"
#include "server_status.h"
#include "source_record.h"
#include "stream_status.h"

/*
    库内维护的Redis表和结构说明

    基本定义：
    挂载点：实体的接收机基站, 发送真实的数据流

    参考站：生成格网点的数据源, 参考站数据并不播发给实际用户
    格网点：虚拟的基站, 由参考站数据推理生成

    用户：实体的用户, 接受数据流数据



    CASTER_CORE配置文件
    Caster_Setting:
        Update_Intv: 1          #内部数据更新频率
        Unactive_Time: 5        #非活动连接判断间隔

    Base_Setting:
        Enable_Mult: false      #允许多连接采用同一挂载点
        Keep_Early: false       #保持较早登录的连接(踢掉同名的新连接)(Enable_Mult为false才生效)

    Rover_Setting:
        Enable_Mult: true       #允许多连接采用同一挂载点
        Keep_Early: false       #保持较早登录的连接(踢掉同名的新连接)(Enable_Mult为false才生效)

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


    MPT:REC:KORO5       类型HASH,  挂载点名-[基站的ConnectKey-添加记录的时间]     记录单个挂载点的详细信息(发布者列表, 一般来说发布者只允许有一个)
    MPT:SUB:KORO5       类型Hash,  挂载点名-[用户的ConnectKey-添加记录的时间]     记录单个挂载点的订阅情况(订阅者列表, 订阅者数量没有限制)
    MPT:LIST:COMMON     类型Hash,          [挂载点名-数据流的挂载点信息]          记录当前在线挂载点的情况, (如果程序不挂掉, 挂载点的维护用不到UTCtime, UTCtime就是为了验证在线的有效性, 并且这个Hash可以一次性查询所有在线挂载点)
    MPT:LIST:ALIAS      类型Hash,          [挂载点名-数据流的挂载点信息]          别名挂载点,挂载点信息与原始挂载点一致
    MPT:LIST:RELAY      类型Hash,          [挂载点名-数据流的挂载点信息]          转发的挂载点,
    MPT:LIST:NEAREST    类型Hash,          [挂载点名-数据流的挂载点信息]          最近挂载点,挂载点信息为手动设置
    MPT:LIST:PROXY      类型Hash,          [挂载点名-数据流的挂载点信息]
    MPT:GEO             类型GEO,           [挂载点名-经度-纬度]                  解析出来的单个挂载点的位置信息
    MPT:STAT            类型HASH,          [基站的ConnectKey-数据流详细信息]      设置对应挂载点的RTCM数据组装规则, Client注册到Catser中的时候, Caster会查询规则, 同时把规则返回给Client, 这样Client就可以知道要如何组装数据了
    MPT:ALIAS           类型HASH

    USR:REC:KORO5       类型HASH,  用户名 -[用户的ConnectKey-添加记录的时间]    记录单个用户的在线情况(对于用户来说, 可以允许多个用户在线)
    USR:SUB:KORO5       类型Hash,  用户名 -[基站的ConnectKey-添加记录的时间]    记录单个用户的订阅情况(订阅者列表, 订阅者数量没有限制)
    USR:LIST:COMMON     类型Hash,          [用户名-账户详细信息(由client主动设置的信息)]
    USR:LIST:RELAY      类型Hash,          [用户名-转发的配置信息？]
    USR:LIST:NEAREST    类型Hash,
    USR:LIST:PROXT      类型Hash,
    USR:GEO             类型GEO,           [用户名-经度-纬度]
    USR:STAT            类型HASH,          [用户的ConnectKey-数据流详细信息]

    CASTER:NODE         类型HASH,           [节点别名-节点状态信息]              记录集群中每个节点的实时状态信息
    CASTER:MASTER       类型HASH                                               由主节点完成数据的更新, 从节点不需要更新
    CASTER:SLAVE        类型HASH                                                从节点
    CASTER:GLOBAL       类型HASH                                                集群的配置信息
    CASTER:OPTION       类型HASH             [节点ID-节点配置]                   各个节点的配置

    STR:RELAY:LIST:        记录数据转发的方式  目标IP:端口  账户 密码   本地挂载点    (所有任务都会同步推送到集群的每个节点上)
    STR:PROXY:LIST


    全局通用的配置,影响集群中的所有Caster (主节点负责更新,从节点仅拉取更新)
        挂载点列表


    影响单个节点的配置
        登录权限管理(配置是否处理基站请求,用户请求,源列表请求)     管理每个节点能够接入的请求

        源列表配置（播发普通挂载点/Alias挂载点/Nearest挂载点/Proxy挂载点/Pull挂载点）  （Alias/Nearest/Proxy）都是虚拟挂载点







    这个设计结构是兼容设计模式, 可以支持多用户和多基站同时在线, 但是只是兼容多用户, 所以对于多用户来说, 并不是特别合理, 譬如如果多个基站都在线, 那么订阅这个基站的人会收到所有发布基站的数据, 订阅用户频道的话, 所有的在线用户发送的消息也都会被订阅者收到
    其实最优的模式应当是直接订阅到ConnectKey？


    GRID:REC:KORO5      类型HASH,  挂载点名-[发布者的ConnectKey-UTCtime]    记录单个挂载点的在线情况(发布者列表, 格网模式的发布者只允许有一个, 允许多个但是实际不会多个同时工作, 只是为了保证分布式节点的同步)
    GRID:SUB:KORO5      类型Hash,  挂载点名-[订阅者的ConnectKey-UTCtime]    记录单个挂载点的订阅情况(订阅者列表, 订阅者数量没有限制)
    GRID:LIST           类型Hash,     [挂载点名-UTCtime]                记录当前在线格网点的情况, (如果程序不挂掉, 挂载点的维护用不到UTCtime, UTCtime就是为了验证在线的有效性, 并且这个Hash可以一次性查询所有在线挂载点)
    GRID:REF:LIST       类型Hash,     [挂载点名-UTCtime]                记录当前在线参考站的情况, (如果程序不挂掉, 挂载点的维护用不到UTCtime, UTCtime就是为了验证在线的有效性, 并且这个Hash可以一次性查询所有在线挂载点)


    GRID:INFO:GEO       类型GEO    [挂载点名-经度-纬度]  人工设置的单个格网点的位置信息
    GRID:INFO:STR       人工设置的挂载点信息, 即每个挂载点
    GRID:INFO:CTRL      [挂载点名-挂载点输出 控制信息] 设置对应挂载点的RTCM数据组装规则, Client注册到Catser中的时候, Caster会查询规则, 同时把规则返回给Client, 这样Client就可以知道要如何组装数据了
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
    CASTER:BROADCAST   //用于所有频道的同步广播, 所有节点的公共频道(Caster_Core之间的同步频道)


    //在线挂载点同步机制


*/

/*
    基站类型：
        1.  对于Common的基站                login_mpt设置为实际的挂载点名称(SHJD01)                alias_mpt也设置为实际的挂载点名称(SHJD01)
        2.  对于NEAREAT挂载点(这个只是存在与配置文件中的挂载点列表中, 并不是真实存在的挂载点)
        3.  对于Pull挂载点(Ntrip Client)    login_mpt设置为本Caster对外服务的挂载点名称(IGS_SP3)    alias_mpt设置为接入第三方的挂载点名称(SSRA03IGS0_SIRGAS2000)
        4.  对于Pull挂载点(TCP Client)      login_mpt设置为本Caster对外服务的挂载点名称(CH01)       alias_mpt设置为接入第三方的备注名(CH01_TCPC)
        5.  对于Pull挂载点(TCP Server)      login_mpt设置为本Caster对外服务的挂载点名称(SN01)       alias_mpt设置为本地TCP端口的备注名(SN01_10009)
        6.  对于Proxy挂载点                 login_mpt设置为Proxy设置挂载点_标识ID(SN-GRECJ_0F64)   alias_mpt设置为第三方的挂载点名称(CMCC-GRECJ)
        7.  对于Alias挂载点                 login_mpt设置为Alias的挂载点名称(SHJD)                 alias_mpt设置为实际挂载点的名称(SHJD01)

    用户类型：
        1.  对于Common的用户               login_mpt设置为实际订阅的挂载点名称(SHJD01)             alias_mpt设置为本地提供数据的挂载点的名称(SHJD01)
        2.  对于NEAREAT用户                login_mpt设置为Nearest挂载点名称(NEAREST)              alias_mpt设置为本地提供数据的挂载点的名称(SHJD01)
        3.  对于Push用户(Ntrip Server)     login_mpt设置为推送给第三方的挂载点名称(SN-SHJD)        alias_mpt设置为本地提供数据的挂载点的名称(SHJD01)
        4.  对于Push用户(TCP Client)       login_mpt设置为推送给第三方的数据流备注(CH01_TCPC)      alias_mpt设置为本地提供数据的挂载点的名称(SHJD01)
        5.  对于Push挂载点(TCP Server)     login_mpt设置为本地TCP端口的备注名(SN01_10009)          alias_mpt设置为本地提供数据的挂载点的名称(SHJD01)
        6.  对于Proxy用户                  login_mpt为提供的Proxy挂载点名称(SN-GRECJ)             alias_mpt设置为Proxy挂载点的(SN-GRECJ_0F64)名称
*/

// 节点状态信息
#define CASTER_MASTER_KEY "CASTER:MASTER"   // 主节点标识, 由主节点写入, 从节点读取, 用于判断当前节点是否为主节点
#define CASTER_NODE_INFO_LIST "CASTER:NODE" // 节点状态列表, 记录集群中每个节点的实时状态信息

// 订阅关系维护
#define MPT_ONLINE_LIST "MPT:LIST" // 挂载点在线列表
#define USR_ONLINE_LIST "USR:LIST" // 用户在线列表

#define MPT_CONNECTION_LIST "MPT:REC" // 某个挂载点的连接列表，记录当前使用这个挂载点名登录的连接的ConnectKey和登录时间
#define USR_CONNECTION_LIST "USR:REC" // 某个用户的连接列表，记录当前使用这个用户名登录的连接的ConnectKey和登录时间

#define MPT_SUBSCRIBE_LIST "MPT:SUB" // 某个挂载点的订阅列表，记录当前订阅这个挂载点的连接的ConnectKey和登录时间
#define USR_SUBSCRIBE_LIST "USR:SUB" // 某个用户的订阅列表，记录当前订阅这个用户数据连接的ConnectKey和登录时间

// 位置信息
#define MPT_POSITION_LIST "MPT:GEO" // 挂载点位置信息列表，记录挂载点的经纬度信息
#define USR_POSITION_LIST "USR:GEO" // 用户位置信息列表，记录用户的经纬度信息

// 源列表信息
#define SOURCE_DECODE_LIST "MPT:SOURCE" // 挂载点信息列表，记录所有在线的挂载点信息
#define SOURCE_RECORD_LIST "MPT:RECORD" // 挂载点记录列表，记录所有的自定义挂载点信息（这个的优先级高于SOURCE:DECODE，当这个有数据的时候，会优先使用这个里面记录的信息）

// 状态维护
#define MPT_STATUS_LIST "MPT:STAT" // 基站状态列表, 记录每个挂载点的状态信息(挂载点的ConnectKey-数据流统计信息)
#define USR_STATUS_LIST "USR:STAT" // 用户状态列表, 记录每个用户的状态信息(用户的ConnectKey-数据流统计信息)
#define STR_STATUS_LIST "STR:STAT" // 数据流状态列表, 记录每个连接的数据流统计信息(基站和用户的连接都记录在这里, 连接key为Mount_Point-ConnectKey)

// 数据转发
#define PULL_STREAM_RECORD "PULL:RECORD" // Pull数据流列表, 记录所有Pull类型的数据转发任务
#define PUSH_STREAM_RECORD "PUSH:RECORD" // Push数据流列表,

#define PULL_STREAM_STATUS "PULL:STAT" // Pull数据流的状态信息
#define PUSH_STREAM_STATUS "PUSH:STAT" // Push数据流的状态信息,

// 权限控制
#define ACCESS_GROUP "ACCESS:GROUP" // 权限组列表, 记录权限组的信息
#define ACCESS_ITEM "ACCESS:ITEM"   // 权限项列表, 记录权限

// 别名维护
#define ALIAS_RULE_LIST "ALIAS:RULE" // 别名规则列表, 记录别名挂载点和实体挂载点的映射关系

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
    // conf  基本配置信息
private:
    int _unactive_time = 10; // 站点更新时间和当前时间差距多少秒会被认为已挂掉
    int _update_intv = 1;
    int _key_expire_time = 30; // Hash键值默认续期时间

    bool _upload_base_stat = true;     // 上报基站数据流统计信息
    bool _upload_rover_stat = true;    // 上报用户数据流统计信息
    bool _download_base_stat = false;  // 下载基站数据流统计信息
    bool _download_rover_stat = false; // 下载用户数据流统计信息

    bool _base_enable_mult = false; // 允许多个同名基站同时在线
    bool _base_keep_early = false;  // 不允许后续同名基站上线(_base_enable_mult=false的时候才生效)

    bool _rover_enable_mult = true; // 允许多个同名用户同时在线
    bool _rover_keep_early = false; // 不允许后续同名用户上线(_rover_enable_mult=false的时候才生效)

    bool _notify_base_inactive = true;  // 当基站不在线的时候, 通知所有订阅该基站的连接
    bool _notify_rover_inactive = true; // 当用户不在线的时候, 通知所有订阅该用户的连接

    std::string _redis_IP;
    int _redis_port;
    std::string _redis_Requirepass;

private:
    std::string _node_ID = util_generate_random_key(6);
    std::string _node_name = "NODE-" + _node_ID;

    event_base *_base;

private:
    // 全局状态，获取整个节点的负载信息
    size_t _server_connection_count = 0; // 当前连接数   MPT:STAT
    size_t _client_connection_count = 0; // 当前连接数   USR:STAT
    size_t _pull_connection_count = 0;   // 当前连接数   STR:PULL:STAT
    size_t _push_connection_count = 0;   // 当前连接数   STR:PUSH:STAT

    // 本地记录  这些数据只需要本地维护和上传，无需下载

    // 频道名(挂载点, 用户名)：[具体连接key:连接回调]
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _base_sub_map;       // channel/connect_key/cb_arg
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _rover_sub_map;      // channel/connect_key/cb_arg
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _base_register_map;  // channel/connect_key/cb_arg
    std::unordered_map<std::string, std::unordered_map<std::string, caster_cb_item>> _rover_register_map; // channel/connect_key/cb_arg
    std::unordered_map<std::string, caster_cb_item> _base_near_sub_map;                                   // 连接key/cb_arg

    // 本节点维护的状态信息 挂载点解析的状态信息
    std::unordered_map<std::string, stream_status> _stream_status_map; // 记录每个连接的数据流统计信息(基站和用户的连接都记录在这里, 连接key为Mount_Point-ConnectKey)
    std::unordered_map<std::string, server_status> _server_status_map; // 基站的状态信息
    std::unordered_map<std::string, client_status> _client_status_map; // 用户的状态信息

    std::unordered_map<std::string, source_record> _source_decode_map;  // 解析的挂载点信息
    std::unordered_map<std::string, source_record> _source_record_map;  // 设置的挂载点信息

    // 集群数据 这些数据需要定期从云端拉取，以减少云端同步的请求压力
    std::unordered_map<std::string, std::string> _active_mount_map;  // 在线挂载点  基站源列表信息 包含转发挂载点        MPT:LIST:COMMON
    std::unordered_map<std::string, std::string> _alias_mount_map;   // 别名挂载点                                     MPT:LIST:ALIAS
    std::unordered_map<std::string, std::string> _nearest_mount_map; // 最近挂载点  挂载点信息                          MPT:LIST:NEAREST
    std::unordered_map<std::string, std::string> _active_user_map;   // 在线用户名  用户基本信息                        USR:LIST:COMMON

    // ALIAS映射关系(如果实体基站不在线，检索一下映射基站，然后从映射的表里找一个当前在线的基站播发数据，如果离线了，那么就再次从这个映射表里找，找到就上线，找不到就下线)
    std::unordered_map<std::string, std::list<std::string>> _alias_rule_map; // 映射关系表             MPT:ALIAS

    std::string _source_list_text;
    std::string _alias_list_text;
    std::string _nearest_list_text;

    long long _startup_time = 0;

public:
    caster_internal();
    ~caster_internal();

    // 返回单例实例
    static caster_internal *getInstance();

    int init(CasterCoreOpt opt, event_base *base);

    int start();
    int stop();

    // 返回Caster的状态信息
    std::string get_status_str();

    // 判断是否是最近挂载点
    bool is_nearest_mpt(std::string mount_point);
    // 判断是否是别名挂载点
    bool is_alias_mpt(std::string mount_point);

    // 注册基站频道 MPT:XXXXXX
    int register_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type);
    // 注销频道
    int withdraw_base_channel(const char *channel, const char *user_name, const char *connect_key);
    // 向频道发布数据
    int pub_base_channel(const char *mount_point, const char *connect_key, const char *data, size_t data_length);
    // 订阅指定频道
    int sub_base_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 订阅最近频道
    int sub_near_channel(const char *channel, const char *user_name, double lat, double lon, const char *connect_key, CasterCallback cb, void *arg);
    // 订阅别名频道
    int sub_alias_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅频道
    int unsub_base_channel(const char *channel, const char *connect_key);
    // 设置基站坐标信息
    int set_base_coord_info(const char *mount_point, const char *connect_key, double ecef_x, double ecef_y, double ecef_z);

    // 设置基站挂载点信息
    int Set_Base_Source_Info(const char *mount_point, const char *connect_key, mount_info);

    // 注册移动站频道 USR:XXXXXX
    int register_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg, CasterRegisterType type);
    // 注销频道
    int withdraw_rover_channel(const char *channel, const char *user_name, const char *connect_key);
    // 向频道发布数据
    int pub_rover_channel(const char *user_name, const char *connect_key, const char *data, size_t data_length);
    // 订阅指定频道
    int sub_rover_channel(const char *channel, const char *user_name, const char *connect_key, CasterCallback cb, void *arg);
    // 取消订阅频道
    int unsub_rover_channel(const char *channel, const char *connect_key);
    // 设置用户坐标信息
    int set_rover_coord_info(const char *user_name, const char *connect_key, double ecef_x, double ecef_y, double ecef_z, int Q, int sat, double diff);

    // 设置延迟信息
    int set_connect_delay_info(const char *connect_key, uint64_t delay);

    // 获取挂载点列表正文
    std::string get_source_list_text();

private:
    // 向注册的基站频道发送状态消息
    int send_status_base_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason);

    // 向注册的移动站频道发送状态消息
    int send_status_rover_channel(const char *channel, const char *connect_key, CasterReply status, const char *reason);

private:
    // 挂载点信息生成的函数
    // std::string convert_mount_info_to_string(mount_info item);
    // mount_info build_default_mount_info(std::string mount_point);

    // 注册回调
    static void Redis_Register_Base_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_Register_Rover_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 订阅一般频道的回调
    static void Redis_SUB_Base_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_SUB_Rover_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 广播频道的回调
    static void Redis_Broadcast_Callback(redisAsyncContext *c, void *r, void *privdata);
    int broadcast_response(std::string req_str); // 从节点执行：Relay任务响应

    // 更新有效挂载点、有效用户的回调
    static void Redis_Update_Active_Base_Callback(redisAsyncContext *c, void *r, void *privdata);  // 拉取MPT:LIST
    static void Redis_Update_Active_Rover_Callback(redisAsyncContext *c, void *r, void *privdata); // 拉取USR:LIST

    static void Redis_Update_Decode_Source_Callback(redisAsyncContext *c, void *r, void *privdata); // 拉取MPT:SOURCE
    static void Redis_Update_Record_Source_Callback(redisAsyncContext *c, void *r, void *privdata); // 拉取MPT:RECORD

    static void Redis_Update_Alias_Rule_Callback(redisAsyncContext *c, void *r, void *privdata); // MPT:ALIAS

    // GRO查询回调
    static void Redis_Geo_Radius_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 查询回调 (传入的privdata 类型 std::unordered_map<std::string, std::string> *
    static void Redis_Get_Hash_Field_Callback(redisAsyncContext *c, void *r, void *privdata);
    // 查询回调 (传入的privdata 类型 std::set<std::string> *
    static void Redis_Get_Set_Value_Callback(redisAsyncContext *c, void *r, void *privdata);

    // 查询回调
    static void Redis_Get_Hash_Lenth_Callback(redisAsyncContext *c, void *r, void *privdata);

    // ---------------------- Redis连接相关函数 --------------------------------------
private:
    int init_sub_context();
    int init_pub_context();

    int subAttemptReconnect();
    int pubAttemptReconnect();

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

public:
    bool _is_pub_connected = false;
    bool _is_sub_connected = false;
    int _pub_reconnect_count = 0; // 重连计数  连接成功后归零   重连失败后, 等待时间0、2、4、8、10(max)
    int _sub_reconnect_count = 0; // 重连计数
    std::string _pub_context_errstr;
    std::string _sub_context_errstr;
    redisAsyncContext *_pub_context = nullptr;
    redisAsyncContext *_sub_context = nullptr;

    // -------------------------------- 定时任务 --------------------------------------
private:
    int clear_overdue_item(); // 清理为空的注册记录

    int upload_record_item();   // 将本地记录的所有连接、挂载点和用户更新到redis中(更新记录时间)
    int download_active_item(); // 将云端记录的在线挂载点更新到本地
    int download_alias_rule();  // 下载别名映射规则

    int check_active_base_channel();  // 检测活跃基站频道(如果已经不存在, 那么就踢出本地连接)
    int check_active_rover_channel(); // 检测活跃基站频道(如果已经不存在, 那么就踢出本地连接)

    int update_alias_source(); // 根据当前在线的挂载点一级别名任务，更新Alias挂载点列表   维护MPT:LIST:ALIAS

    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    event *_timeout_ev;
    timeval _timeout_tv;

    // --------------------------- 主节点任务 ------------------------------------
    // 主节点任务
    // 获取整个集群的信息
    // CASTER:MASTER
    // CASTER:NODE

    // 全量获取当前的转发任务
    // STR:RELAY:LIST   // 任务列表   和参数信息  （共同生成一个哈希值，作为Key值）
    // 获取各个任务的执行状态
    // STR:RELAY:STAT   // 任务执行情况 和参数信息  （任务Key，状态）
    // 根据当前已有节点数量，将任务分配到各个节点
    // 考虑各个节点的负载数量
    // 向指定的频道发送广播（执行任务，关闭任务）（修改任务=关闭任务+新建任务）
    // 执行任务：LIST中有但是STAT中还没有，关闭任务：STAT中有但是LIST中没有

    std::unordered_map<std::string, std::string> _cluster_node_map;

    std::unordered_map<std::string, pull_record> _pull_record_map; // 拉取连接的任务信息
    std::unordered_map<std::string, push_record> _push_record_map; // 推送连接的任务信息
    std::unordered_map<std::string, pull_status> _pull_status_map; // 拉取连接的状态信息
    std::unordered_map<std::string, push_status> _push_status_map; // 推送连接的状态信息
                                                                   //  流的信息不在这里统计

    int try_set_master_node();          // 尝试设置为主节点
    int sync_cluster_state();           // 主节点同步全局信息到本地
    int relay_pull_task_distribution(); // 主节点执行：Relay任务分发
    int relay_push_task_distribution(); // 主节点执行：Relay任务分发

    // 清理已经失效的GEO节点信息（查询是否已经是在线的挂载点，不是那么直接删除）

    // 节点频道的回调
    static void Redis_SetMaster_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_KeepMaster_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_SyncClusterNode_Callback(redisAsyncContext *c, void *r, void *privdata); // 获取所有节点信息
    static void Redis_SyncPullList_Callback(redisAsyncContext *c, void *r, void *privdata);    // 获取所有的转发任务
    static void Redis_SyncPullStat_Callback(redisAsyncContext *c, void *r, void *privdata);    // 获取所有的转发任务状态
    static void Redis_SyncPushList_Callback(redisAsyncContext *c, void *r, void *privdata);    // 获取所有的转发任务
    static void Redis_SyncPushStat_Callback(redisAsyncContext *c, void *r, void *privdata);    // 获取所有的转发任务状态

    // ---------------------------- 从节点任务 -----------------------------------
    // 从节点任务
    // 尝试抢占主节点，抢占完成后，接管，触发主节点任务
    // 设置节点 NX，获取节点，判断自己是不是主节点，如果是主节点，给主节点续期，执行主节点任务

    // 监听指定频道，根据接收到的信息执行任务（关闭任务/修改任务）刷新任务

public:
    int update_pull_base_info(const char *mount_point, const char *alias_mpt, const char *connect_key, int state);
    int update_push_rover_info(const char *mount_point, const char *alias_mpt, const char *connect_key, int state);

private:
    // 上报任务执行状态
    // 上报自己的状态
    int upload_node_status();                     // 上传当前节点的状态   上传到CASTER:NODE中添加一条记录
    int relay_task_response(std::string req_str); // 从节点执行：Relay任务响应
    int upload_relay_status();                    // 从节点上报本地已执行的Relay任务

    static void Redis_NodeChannel_Callback(redisAsyncContext *c, void *r, void *privdata);

    // ---------------------- 数据流统计 --------------------------------------
private:
    double _send_total = 0;         // 总发送字节数
    double _send_speed = 0.0;       // 总发送速度
    double _recv_total = 0;         // 总接收字节数
    double _recv_speed = 0.0;       // 总接收速度
    std::time_t _update_time = 0.0; // 信息更新时刻(执行所有函数的时候, 都会更新一下这个函数)

    struct Sample
    {
        int64_t time; // 秒级时间戳
        double bytes;
    };

    std::deque<Sample> _recvHistory;
    std::deque<Sample> _sendHistory;

    int _windowSize = 5; // 窗口秒数

    // 清理超出窗口的样本
    void cleanOld(std::deque<Sample> &history, int64_t now);

    // 计算平均速度
    double calcAvgSpeed(const std::deque<Sample> &history) const;
    int add_sum_recv(int size);
    int add_sum_send(int size);

    // ---------------------- Event执行延迟测试 --------------------------------------
private:
    int test_queue_delay(); // 测试延迟信息更新

    static void TestDelayCallback(evutil_socket_t fd, short events, void *arg);

private:
    event *_testdelay_ev;

    std::chrono::high_resolution_clock::time_point _activate_time; // 激活时间
    std::chrono::high_resolution_clock::time_point _execute_time;  // 执行时间
    int64_t _queue_delay = 0;                                      // 时间延迟

    // ------------------------ Redis连接延迟测试 --------------------------------------
public:
    int check_redis_connection();

    static void Redis_Sub_Ping_Callback(redisAsyncContext *c, void *r, void *privdata);
    static void Redis_Pub_Ping_Callback(redisAsyncContext *c, void *r, void *privdata);

private:
    int _sub_ping_fail_count = 0;
    int _pub_ping_fail_count = 0;

    std::chrono::high_resolution_clock::time_point _sub_ping_time; // 激活时间
    std::chrono::high_resolution_clock::time_point _sub_pong_time; // 执行时间
    int64_t _sub_ping_delay = 0;                                   // 时间延迟
    int64_t _sub_tcp_delay = 0;                                    // 时间延迟

    std::chrono::high_resolution_clock::time_point _pub_ping_time; // 激活时间
    std::chrono::high_resolution_clock::time_point _pub_pong_time; // 执行时间
    int64_t _pub_ping_delay = 0;                                   // 时间延迟
    int64_t _pub_tcp_delay = 0;                                    // 时间延迟

    // ------------------- Relay任务处理 --------------------------------------
public:
    int relay_register_callback(RelayCallback cb, void *arg);

private:
    void *_relay_cb_arg = nullptr;
    RelayCallback _relay_cb = nullptr;
};
