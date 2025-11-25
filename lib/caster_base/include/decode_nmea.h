#pragma once
#include <string>
#include <vector>
// 流式RTCM解析器 Service和Monitor共用的解析器
//  对于Service来说，只需要解析基本的坐标信息即可
//  对于Monitor来说，可以解析更多信息，比如有哪些报文，数据的频率，坐标、不同系统的卫星数、卫星频点情况

class decode_nmea
{
public:
    // 存储解析的坐标
    bool _has_position = false;
    double _ecef_x = 0.0;
    double _ecef_y = 0.0;
    double _ecef_z = 0.0;

    int _quality = 0;   // 定位状态
    int _sat_num = 0;   // 卫星数
    double _diff = 0.0; // 差分延迟

    // 解析的坐标最后更新时间
    time_t _position_update_time = 0;

private:
    size_t _msg_min_length = 3;        // 不同的派生类在创建的时候根据报文类型来定义最短长度
    size_t _msg_max_recycle = 1000000; // 最多回收的字节数，如果超过了这个字节数，那么就直接清空，避免一直堆积

    std::vector<uint8_t> _data; // 未解析/新添加数据缓冲区
    std::vector<uint8_t> _msg;  // 单条报文

    bool _recycle_back_flag = false;
    bool _recycle_front_flag = false;
    std::vector<uint8_t> _recycle_back;  // 反向解析缓冲区，如果是反向添加的数据，那么没能成功解析的字节会放入到这里面，等待下次反向数据推入后重新拼接
    std::vector<uint8_t> _recycle_front; // 正向解析缓冲区，如果是正向添加的数据，那么没能成功解析的字节会放入到这里面，等待下次正向数据推入后重新拼接

public:
    decode_nmea(/* args */) {};
    ~decode_nmea() {};

    int Decode(const char *buffer, size_t bufLen, bool reverse = false, bool is_end = false);

protected:
    int check_header(bool last_chunk); // 解析报文头

    int get_message(size_t msg_length = 0);

    int decode_message(int id);

private:
    int decode_GGA();
    int decode_RMC();
    int decode_GSV();
    int decode_GSA();
    int decode_ZDA();
};
