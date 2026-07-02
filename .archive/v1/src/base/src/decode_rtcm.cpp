#include "decode_rtcm.h"
#include "knt.h"
#include <algorithm>
#include <cmath>

static uint32_t calCRC24(const uint8_t *buf, size_t size)
{
    const uint32_t POLY_CRC24 = 0x01864CFB;

    uint32_t crc = 0;
    int ii;
    while (size--)
    {
        crc ^= (*buf++) << (16);
        for (ii = 0; ii < 8; ii++)
        {
            crc <<= 1;
            if (crc & 0x1000000)
                crc ^= POLY_CRC24;
        }
    }
    return crc;
}

static uint64_t GET_BBits(const std::vector<uint8_t> &data, size_t bit_pos, size_t bit_length)
{
    uint64_t result = 0;
    size_t byte_pos = bit_pos / 8;
    size_t bit_offset = bit_pos % 8;

    // 处理位偏移
    while (bit_length > 0)
    {
        uint8_t current_byte = data[byte_pos];
        size_t bits_to_extract = std::min(bit_length, 8 - bit_offset);

        result <<= bits_to_extract;
        result |= (current_byte >> bit_offset) & ((1 << bits_to_extract) - 1);

        bit_length -= bits_to_extract;
        byte_pos++;
        bit_offset = 0;
    }

    return result;
}

static uint64_t util_getbitu_64(const uint8_t *buff, int pos, int len)
{
    uint64_t bits = 0;
    int i;
    for (i = pos; i < pos + len; i++)
        bits = (bits << 1) + ((buff[i / 8] >> (7 - i % 8)) & 1u);
    return bits;
}

static int64_t util_getbits_64(const uint8_t *buff, int pos, int len)
{
    uint64_t bits = util_getbitu_64(buff, pos, len);
    if (len <= 0 || 64 <= len || !(bits & (1ULL << (len - 1))))
        return (int64_t)bits;
    return (int64_t)(bits | (~0ULL << len)); /* extend sign */
}

static double util_getbitg_64(const uint8_t *buff, int pos, int len)
{
    double value = util_getbitu_64(buff, pos + 1, len - 1);
    return util_getbitu_64(buff, pos, 1) ? -value : value;
}

static uint64_t util_getbitu_64_auto(std::vector<uint8_t> &data, size_t &pos, size_t len)
{
    unsigned char *buff = data.data();
    uint64_t result = util_getbitu_64(buff, pos, len);
    pos += len;
    return result;
}

static int64_t util_getbits_64_auto(std::vector<uint8_t> &data, size_t &pos, size_t len)
{
    unsigned char *buff = data.data();
    int64_t result = util_getbits_64(buff, pos, len);
    pos += len;
    return result;
}

static double util_getbitg_64_auto(std::vector<uint8_t> &data, size_t &pos, size_t len)
{
    unsigned char *buff = data.data();
    double result = util_getbitg_64(buff, pos, len);
    pos += len;
    return result;
}

int decode_rtcm::Decode(const char *buffer, size_t bufLen, bool reverse, bool is_end)
{
    if(_recycle_back.size()>_msg_max_recycle)
    {
        _recycle_back.clear();
    }
    if(_recycle_front.size()>_msg_max_recycle)
    {
        _recycle_front.clear();
    }

    if (reverse)
    {
        _data.insert(_data.begin(), reinterpret_cast<const uint8_t *>(buffer),
                     reinterpret_cast<const uint8_t *>(buffer) + bufLen);
        _data.insert(_data.end(), _recycle_back.begin(), _recycle_back.end());
    }
    else
    {
        // 残余放前面，新数据追加在后面
        _data.insert(_data.begin(), _recycle_front.begin(), _recycle_front.end());
        _data.insert(_data.end(), reinterpret_cast<const uint8_t *>(buffer),
                     reinterpret_cast<const uint8_t *>(buffer) + bufLen);
    }

    // 清除上次缓存
    _recycle_back.clear();
    _recycle_front.clear();
    // 重置回收标志
    _recycle_back_flag = false;
    _recycle_front_flag = false;

    while (_data.size() > _msg_min_length)
    {
        int msg_length = check_header(is_end);
        if (msg_length == 0) // 头检查（返回0，表示头不正确）
        {
            if (_recycle_back_flag == false) // 还没有进行回收
            {
                _recycle_back.push_back(_data.front()); // 数据放入回收区
            }
            _data.erase(_data.begin());
            continue; // 头检测失败，跳转到下一个字符
        }

        if (msg_length > _data.size()) // 剩余字符串已经少于报文长度
        {
            _recycle_front.insert(_recycle_front.begin(), _data.begin(), _data.end());
            _recycle_front_flag = true;
            _data.clear();
            return 0; // 当前数据区数据不足
        }

        // 头检测成功 进入下一步
        int id = get_message(msg_length); // 如果返回0，则表明数据数据解析失败，表明虽然字节头和长度都正确，但是其实报文是不正确的
        if (id == 0)
        {
            if (_recycle_back_flag == false) // 还没有进行回收
            {
                _recycle_back.push_back(_data.front()); // 数据放入回收区
            }
            _data.erase(_data.begin());
            continue; // 头检测失败，跳转到下一个字符
        }

        // 报文解析成功
        _recycle_front_flag = true; // 第一次返回的时候成功的报文，回收就已经完成了，后续的数据不需要再回收
        decode_message(id);
        _msg.clear();
    }

    if (reverse)
    {
        _recycle_back.insert(_recycle_back.end(), _data.begin(), _data.end());
        _recycle_back_flag = true;
    }
    else
    {
        _recycle_front.insert(_recycle_front.end(), _data.begin(), _data.end());
        _recycle_front_flag = true;
    }
    _data.clear();

    return 0;
}

int decode_rtcm::check_header(bool last_chunk)
{
    // 查找RTCM头

    // RTCM 3 message format:
    // +----------+--------+-----------+--------------------+----------+
    // | preamble | 000000 |  length   |    data message    |  parity  |
    // +----------+--------+-----------+--------------------+----------+
    // |<-- 8 --->|<- 6 -->|<-- 10 --->|<--- length x 8 --->|<-- 24 -->|

    if (_data.front() != 0xD3)
    {
        return 0;
    }

    int length = ((_data[1] & 0x03) << 8) | (_data[2]);
    int length2 = GET_BBits(_data, 14, 10);

    size_t message_length = 3 + length + 3; // Sync + Header + Message + CRC
    return message_length;
}

int decode_rtcm::get_message(size_t msg_length)
{
    // 查找RTCM头

    // RTCM 3 message format:
    // +----------+--------+-----------+--------------------+----------+
    // | preamble | 000000 |  length   |    data message    |  parity  |
    // +----------+--------+-----------+--------------------+----------+
    // |<-- 8 --->|<- 6 -->|<-- 10 --->|<--- length x 8 --->|<-- 24 -->|

    if (_data.front() != 0xD3)
    {
        return 0; // 头不正确
    }

    int length = ((_data[1] & 0x03) << 8) | (_data[2]);
    int length2 = GET_BBits(_data, 14, 10);

    size_t message_length = 3 + length + 3; // Sync + Header + Message + CRC

    // if (message_length > _data.size())
    // {
    //     return 0; // 当前数据区数据不足
    // }

    // 执行 CRC 校验
    // uint32_t calculated_crc = calCRC24(_msg.data(), 3 + length);
    uint32_t calculated_crc = calCRC24(_data.data(), 3 + length);
    uint32_t extracted_crc = (static_cast<uint32_t>(_data[3 + length]) << 16) |
                             (static_cast<uint32_t>(_data[3 + length + 1]) << 8) |
                             static_cast<uint32_t>(_data[3 + length + 2]);
    uint32_t extracted_crc2 = GET_BBits(_data, (3 + length) * 8, 24);

    if (calculated_crc != extracted_crc)
    {
        return 0; // CRC校验失败
    }

    // 校验成功，从_data中提取完整报文
    _msg.assign(_data.begin(), _data.begin() + message_length);
    _data.erase(_data.begin(), _data.begin() + message_length);

    return (_msg[3] << 4) | (_msg[4] >> 4);
}

int decode_rtcm::decode_message(int id)
{
    // 记录报文统计
    auto now = util_get_now_second();
    auto &stat = _msg_stats[id];
    if (stat.count == 0)
        stat.first_time = now;
    stat.last_time = now;
    stat.count++;

    // 滑动窗口计算播发间隔
    stat.window_count++;
    if (stat.window_count == 1)
    {
        stat.window_start = now;
    }
    else
    {
        auto elapsed = now - stat.window_start;
        if (elapsed >= 300) // 300秒窗口
        {
            stat.interval = static_cast<int>(std::round(static_cast<double>(elapsed) / (stat.window_count - 1)));
            if (stat.interval < 1) stat.interval = 1;
            // 重置窗口
            stat.window_count = 1;
            stat.window_start = now;
        }
    }

    // 根据MSM报文推断卫星系统
    if (id >= 1071 && id <= 1077)
        _gps_indicator = true;
    else if (id >= 1081 && id <= 1087)
        _glo_indicator = true;
    else if (id >= 1091 && id <= 1097)
        _gal_indicator = true;
    else if (id >= 1101 && id <= 1107)
        _sbas_indicator = true;
    else if (id >= 1111 && id <= 1117)
        _qzss_indicator = true;
    else if (id >= 1121 && id <= 1127)
        _bds_indicator = true;
    else if (id >= 1131 && id <= 1137)
        _navic_indicator = true;

    if ((id >= 1057 && id <= 1068) ||
        (id >= 1240 && id <= 1270) ||
        (id == 4076))
    {
        // RTCM SSR
        // IGS SSR
    }
    else if (id >= 1070 && id <= 1237)
    {
        /* MSM */
    }
    else
    {
        switch (id)
        {
        case 1005:
            decode_M1005();
            break;
        case 1006:
            decode_M1006();
            break;
        case 1032:
            decode_M1032();
            break;
        case 1019:

            break;
        case 1020:

            break;
        case 1041:

            break;
        case 1042:

            break;
        case 1043:

            break;
        case 1044:

            break;
        case 1045:

            break;
        case 1046:

            break;
        case 63:
            break; // decode_type1042(rtcm);  /* RTCM draft */
        }
    }

    return 0;
}

int decode_rtcm::decode_M1005()
{
    size_t ctx = 3 * 8;
    // 根据报文获取卫星系统
    int messageID = util_getbitu_64_auto(_msg, ctx, 12);
    int ref_id = util_getbitu_64_auto(_msg, ctx, 12);
    int resv = util_getbitu_64_auto(_msg, ctx, 6);
    int GPS_indicator = util_getbitu_64_auto(_msg, ctx, 1);
    int GLO_indicator = util_getbitu_64_auto(_msg, ctx, 1);
    int GAL_indicator = util_getbitu_64_auto(_msg, ctx, 1);
    int ref_indicator = util_getbitu_64_auto(_msg, ctx, 1);

    double ecef_x = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    util_getbitu_64_auto(_msg, ctx, 1);
    util_getbitu_64_auto(_msg, ctx, 1);
    double ecef_y = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    util_getbitu_64_auto(_msg, ctx, 2);
    double ecef_z = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;

    // 更新卫星系统标志
    if (GPS_indicator) _gps_indicator = true;
    if (GLO_indicator) _glo_indicator = true;
    if (GAL_indicator) _gal_indicator = true;

    _position_update_time=util_get_now_second();
    _has_position = true;
    _ecef_x = ecef_x;
    _ecef_y = ecef_y;
    _ecef_z = ecef_z;

    return 0;
}

int decode_rtcm::decode_M1006()
{
    size_t ctx = 3 * 8;
    // 根据报文获取卫星系统
    int messageID = util_getbitu_64_auto(_msg, ctx, 12);
    int ref_id = util_getbitu_64_auto(_msg, ctx, 12);
    int resv = util_getbitu_64_auto(_msg, ctx, 6);
    int GPS_indicator = util_getbitu_64_auto(_msg, ctx, 1);
    int GLO_indicator = util_getbitu_64_auto(_msg, ctx, 1);
    int GAL_indicator = util_getbitu_64_auto(_msg, ctx, 1);
    int ref_indicator = util_getbitu_64_auto(_msg, ctx, 1);

    double ecef_x = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    util_getbitu_64_auto(_msg, ctx, 1);
    util_getbitu_64_auto(_msg, ctx, 1);
    double ecef_y = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    util_getbitu_64_auto(_msg, ctx, 2);
    double ecef_z = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    double ant_h = util_getbitu_64_auto(_msg, ctx, 16) * 0.0001;

    // 更新卫星系统标志
    if (GPS_indicator) _gps_indicator = true;
    if (GLO_indicator) _glo_indicator = true;
    if (GAL_indicator) _gal_indicator = true;

    _position_update_time=util_get_now_second();
    _has_position = true;
    _ecef_x = ecef_x;
    _ecef_y = ecef_y;
    _ecef_z = ecef_z;

    return 0;
}

int decode_rtcm::decode_M1032()
{
    size_t ctx = 3 * 8;
    // 根据报文获取卫星系统
    int messageID = util_getbitu_64_auto(_msg, ctx, 12);
    int non_id = util_getbitu_64_auto(_msg, ctx, 12);
    int ref_id = util_getbitu_64_auto(_msg, ctx, 12);
    int resv = util_getbitu_64_auto(_msg, ctx, 6);
    double ecef_x = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    double ecef_y = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;
    double ecef_z = util_getbits_64_auto(_msg, ctx, 38) * 0.0001;

    _position_update_time=util_get_now_second();
    _has_position = true;
    _ecef_x = ecef_x;
    _ecef_y = ecef_y;
    _ecef_z = ecef_z;

    return 0;
}

std::string decode_rtcm::get_format_details() const
{
    // 按报文ID排序输出，括号内为播发间隔(秒/条)，如 "1005(10),1074(1)" 表示1005每10秒一条，1074每秒一条
    std::vector<std::pair<int, int>> sorted_msgs;
    for (const auto &item : _msg_stats)
    {
        sorted_msgs.emplace_back(item.first, item.second.interval);
    }
    std::sort(sorted_msgs.begin(), sorted_msgs.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    std::string result;
    for (const auto &item : sorted_msgs)
    {
        if (!result.empty())
            result += ",";
        result += std::to_string(item.first) + "(" + std::to_string(item.second) + ")";
    }
    return result;
}

std::string decode_rtcm::get_nav_system() const
{
    std::string result;
    if (_gps_indicator) result += "GPS";
    if (_glo_indicator) { if (!result.empty()) result += "+"; result += "GLO"; }
    if (_gal_indicator) { if (!result.empty()) result += "+"; result += "GAL"; }
    if (_bds_indicator) { if (!result.empty()) result += "+"; result += "BDS"; }
    if (_qzss_indicator) { if (!result.empty()) result += "+"; result += "QZSS"; }
    if (_sbas_indicator) { if (!result.empty()) result += "+"; result += "SBAS"; }
    if (_navic_indicator) { if (!result.empty()) result += "+"; result += "NavIC"; }
    return result;
}
