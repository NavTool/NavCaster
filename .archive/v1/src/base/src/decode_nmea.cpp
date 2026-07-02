#include <string>
#include "decode_nmea.h"
#include "knt.h"

static bool checkNMEA(const std::vector<uint8_t> &msg)
{
    if (msg.size() < 9)
        return false; // 最短 NMEA 如 $A*00\r\n

    // 找起始 '$'
    auto it_start = std::find(msg.begin(), msg.end(), '$');
    if (it_start == msg.end())
        return false;

    // 找 '*'
    auto it_star = std::find(it_start, msg.end(), '*');
    if (it_star == msg.end())
        return false;

    // '*' 后必须至少有两位校验和
    if (std::distance(it_star, msg.end()) < 3)
        return false;

    // 计算 XOR 校验
    uint8_t sum = 0;
    for (auto it = it_start + 1; it != it_star; ++it)
        sum ^= *it;

    // 取报文中的校验和（两个 ASCII 码表示的十六进制）
    uint8_t high = it_star[1];
    uint8_t low = it_star[2];

    // 转成数字
    auto hexToVal = [](uint8_t c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };

    int highVal = hexToVal(high);
    int lowVal = hexToVal(low);
    if (highVal < 0 || lowVal < 0)
        return false;

    uint8_t msgSum = (highVal << 4) | lowVal;

    return sum == msgSum;
}

static std::vector<std::string> splitGGA(const std::string &nmea)
{
    std::vector<std::string> fields;
    std::string s = nmea;

    // 去掉前导空白
    while (!s.empty() && (s[0] == ' ' || s[0] == '\r' || s[0] == '\n'))
        s.erase(s.begin());

    // 必须以 $ 开头
    if (s.empty() || s[0] != '$')
        return fields;

    // 找到 *
    size_t starPos = s.find('*');
    if (starPos != std::string::npos)
        s = s.substr(1, starPos - 1); // 去掉 $, 去掉 * 后面的校验和
    else
        s = s.substr(1); // 只有 $ 没有校验和（低端设备可能）

    // 按 , 拆分
    size_t start = 0;
    while (true)
    {
        size_t comma = s.find(',', start);
        if (comma == std::string::npos)
        {
            fields.push_back(s.substr(start));
            break;
        }
        fields.push_back(s.substr(start, comma - start));
        start = comma + 1;
    }

    return fields;
}

int decode_nmea::Decode(const char *buffer, size_t bufLen, bool reverse, bool is_end)
{
    if (_recycle_back.size() > _msg_max_recycle)
    {
        _recycle_back.clear();
    }
    if (_recycle_front.size() > _msg_max_recycle)
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

int decode_nmea::check_header(bool last_chunk)
{
    // 查找NMEA头
    if (_data.front() != '$') // $ 开头
    {
        return 0;
    }

    auto it = std::find(_data.begin(), _data.end(), '\r');
    if (it != _data.end() && (it + 1) != _data.end() && *(it + 1) == '\n')
    {
        return static_cast<size_t>(std::distance(_data.begin(), it));
    }
    else
    {
        return _data.size() + 1; // 未找到 CRLF
    }
}

int decode_nmea::get_message(size_t msg_length)
{
    _msg.assign(_data.begin(), _data.begin() + msg_length);
    _data.erase(_data.begin(), _data.begin() + msg_length);

    // 做校验和
    if (checkNMEA(_msg))
    {
        std::string text(_msg.begin(), _msg.end()); // 转成string

        if (text.substr(3, 3) == "GGA")
        {
            return 1;
        }
        else if (text.substr(3, 3) == "RMC")
        {
            return 2;
        }
        else if (text.substr(3, 3) == "ZDA")
        {
            return 3;
        }
    }
    return 0;
}

int decode_nmea::decode_message(int id)
{
    switch (id)
    {
    case 1:
        decode_GGA();
        break;
    case 2:
        decode_RMC();
        break;
    case 3:
        decode_ZDA();
        break;
    case 0:
        return 0;
    default:
        break;
    }

    return 0;
}

int decode_nmea::decode_GGA()
{
    /*
                                                          11
            1         2       3 4        5 6 7  8   9  10 |  12 13  14   15
            |         |       | |        | | |  |   |   | |   | |   |    |
    $--GGA,hhmmss.ss,ddmm.mm,a,ddmm.mm,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh<CR><LF>
    */

    std::string str(_msg.begin(), _msg.end());

    auto fields = splitGGA(str);

    if (fields.size() == 15)
    {
        double utc = 0; // 秒（0~86400）
        double lat = 0; // 十进制度
        double lon = 0;
        int fixQuality = std::stoi(fields[6]); // 定位质量
        int numSat = std::stoi(fields[7]);     // 卫星数量
        double hdop = std::stod(fields[8]);
        double alt = std::stod(fields[9]);       // 海拔（米）
        double geoid = std::stod(fields[11]);    // 大地水准面分离
        double diffAge = std::stod(fields[13]);  // 差分龄期
        int diffStation = std::stoi(fields[14]); // 差分站号

        // UTC 天内秒
        {
            double hh = std::stoi(fields[1].substr(0, 2));
            double mm = std::stoi(fields[1].substr(2, 2));
            double ss = std::stod(fields[1].substr(4));
            utc = hh * 3600 + mm * 60 + ss;
        }

        // 纬度
        {
            size_t dot = fields[2].find('.');
            int degree_len = (dot > 4) ? 3 : 2;
            double deg = std::stod(fields[2].substr(0, degree_len));
            double min = std::stod(fields[2].substr(degree_len));
            lat = deg + (min / 60.0);

            if (fields[3] == "S")
            {
                lat = -lat;
            }
        }
        // 经度
        {
            size_t dot = fields[4].find('.');
            int degree_len = (dot > 4) ? 3 : 2;
            double deg = std::stod(fields[4].substr(0, degree_len));
            double min = std::stod(fields[4].substr(degree_len));
            lon = deg + (min / 60.0);

            if (fields[5] == "W")
            {
                lon = -lon;
            }
        }

        double ell = alt + geoid;
        double ecef_x = 0.0, ecef_y = 0.0, ecef_z = 0.0;
        util_pos2ecef(lat, lon, ell, ecef_x, ecef_y, ecef_z);

        _position_update_time = util_get_now_second();
        _has_position = true;
        _ecef_x = ecef_x;
        _ecef_y = ecef_y;
        _ecef_z = ecef_z;

        _quality = fixQuality;
        _sat_num = numSat;
        _diff = diffAge;
    }

    return 0;
}

int decode_nmea::decode_RMC()
{
    return 0;
}

int decode_nmea::decode_GSV()
{
    return 0;
}

int decode_nmea::decode_GSA()
{
    return 0;
}

int decode_nmea::decode_ZDA()
{
    return 0;
}
