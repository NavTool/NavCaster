#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <ctime>
// 流式RTCM解析器 Service和Monitor共用的解析器
//  对于Service来说，只需要解析基本的坐标信息即可
//  对于Monitor来说，可以解析更多信息，比如有哪些报文，数据的频率，坐标、不同系统的卫星数、卫星频点情况

// RTCM MSM 报文ID与卫星系统的映射关系
// GPS:  1071-1077   GLO:  1081-1087   GAL:  1091-1097
// SBAS: 1101-1107   QZSS: 1111-1117   BDS:  1121-1127
// NavIC:1131-1137

class decode_rtcm
{
public:

    // 存储解析的坐标
    bool _has_position = false;
    double _ecef_x = 0.0;
    double _ecef_y = 0.0;
    double _ecef_z = 0.0;
    // 解析的坐标最后更新时间
    time_t _position_update_time = 0;

    // RTCM报文统计信息
    struct MsgStat
    {
        int count = 0;         // 收到的报文总计数
        time_t first_time = 0; // 首次收到时间
        time_t last_time = 0;  // 最后收到时间

        // 滑动窗口统计播发间隔
        int window_count = 0;     // 当前窗口内的报文计数
        time_t window_start = 0;  // 当前窗口起始时间
        int interval = 1;         // 计算出的播发间隔(秒/条)
    };
    std::unordered_map<int, MsgStat> _msg_stats; // 报文ID → 统计

    // 从1005/1006中解析出的卫星系统标志
    bool _gps_indicator = false;
    bool _glo_indicator = false;
    bool _gal_indicator = false;
    bool _bds_indicator = false;
    bool _qzss_indicator = false;
    bool _sbas_indicator = false;
    bool _navic_indicator = false;

    // 根据统计信息生成源列表所需字段
    std::string get_format_details() const;
    std::string get_nav_system() const;

private:
    size_t _msg_min_length=3; //不同的派生类在创建的时候根据报文类型来定义最短长度
    size_t _msg_max_recycle=1000000; // 最多回收的字节数，如果超过了这个字节数，那么就直接清空，避免一直堆积


    std::vector<uint8_t> _data; // 未解析/新添加数据缓冲区
    std::vector<uint8_t> _msg;  // 单条报文

    bool _recycle_back_flag = false;
    bool _recycle_front_flag = false;
    std::vector<uint8_t> _recycle_back;  // 反向解析缓冲区，如果是反向添加的数据，那么没能成功解析的字节会放入到这里面，等待下次反向数据推入后重新拼接
    std::vector<uint8_t> _recycle_front; // 正向解析缓冲区，如果是正向添加的数据，那么没能成功解析的字节会放入到这里面，等待下次正向数据推入后重新拼接

public:
    decode_rtcm(/* args */) {};
    ~decode_rtcm() {};

    int Decode(const char *buffer, size_t bufLen, bool reverse = false,bool is_end=false);

protected:
    int check_header(bool last_chunk); // 解析报文头

    int get_message(size_t msg_length = 0);

    int decode_message(int id);

private:

    int decode_M1005();
    int decode_M1006();
    int decode_M1032();

};
