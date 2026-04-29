#include "license_codec.h"
#include "register.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace
{
// 与历史实现保持一致的对称密钥 (16 字节, 后部由编译器零填充)
constexpr char SECRET_KEY[16] = "caster_sino";

// IEEE 802.3 标准 CRC32
uint32_t crc32(const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    static bool inited = false;
    if (!inited)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        inited = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void pack(uint8_t buf[16], const license_info &info, uint32_t fp)
{
    buf[0] = 'N';
    buf[1] = 'C';
    buf[2] = 0x01; // version
    buf[3] = static_cast<uint8_t>((info.enable_pull ? 0x01 : 0) | (info.enable_push ? 0x02 : 0));

    buf[4] = static_cast<uint8_t>(fp & 0xFF);
    buf[5] = static_cast<uint8_t>((fp >> 8) & 0xFF);
    buf[6] = static_cast<uint8_t>((fp >> 16) & 0xFF);
    buf[7] = static_cast<uint8_t>((fp >> 24) & 0xFF);

    uint32_t ed = info.expire_days;
    buf[8]  = static_cast<uint8_t>(ed & 0xFF);
    buf[9]  = static_cast<uint8_t>((ed >> 8) & 0xFF);
    buf[10] = static_cast<uint8_t>((ed >> 16) & 0xFF);
    buf[11] = static_cast<uint8_t>((ed >> 24) & 0xFF);

    int sl_int = info.server_limit;
    int cl_int = info.client_limit;
    if (sl_int < 0) sl_int = 0;
    if (cl_int < 0) cl_int = 0;
    if (sl_int > 0xFFFF) sl_int = 0xFFFF;
    if (cl_int > 0xFFFF) cl_int = 0xFFFF;
    uint16_t sl = static_cast<uint16_t>(sl_int);
    uint16_t cl = static_cast<uint16_t>(cl_int);
    buf[12] = static_cast<uint8_t>(sl & 0xFF);
    buf[13] = static_cast<uint8_t>((sl >> 8) & 0xFF);
    buf[14] = static_cast<uint8_t>(cl & 0xFF);
    buf[15] = static_cast<uint8_t>((cl >> 8) & 0xFF);
}

bool unpack(const uint8_t buf[16], uint32_t expect_fp, license_info &info)
{
    if (buf[0] != 'N' || buf[1] != 'C') return false;
    if (buf[2] != 0x01) return false;

    uint32_t fp = static_cast<uint32_t>(buf[4]) | (static_cast<uint32_t>(buf[5]) << 8) |
                  (static_cast<uint32_t>(buf[6]) << 16) | (static_cast<uint32_t>(buf[7]) << 24);
    if (fp != expect_fp) return false;

    info.enable_pull = (buf[3] & 0x01) != 0;
    info.enable_push = (buf[3] & 0x02) != 0;
    info.expire_days = static_cast<uint32_t>(buf[8]) | (static_cast<uint32_t>(buf[9]) << 8) |
                       (static_cast<uint32_t>(buf[10]) << 16) | (static_cast<uint32_t>(buf[11]) << 24);
    info.server_limit = static_cast<int>(static_cast<uint32_t>(buf[12]) | (static_cast<uint32_t>(buf[13]) << 8));
    info.client_limit = static_cast<int>(static_cast<uint32_t>(buf[14]) | (static_cast<uint32_t>(buf[15]) << 8));
    return true;
}
} // namespace

namespace license_codec
{
std::string normalize_mac(const std::string &mac)
{
    std::string out;
    out.reserve(mac.size());
    for (char ch : mac)
    {
        unsigned char uc = static_cast<unsigned char>(ch);
        if (std::isxdigit(uc))
            out.push_back(static_cast<char>(std::tolower(uc)));
    }
    return out;
}

uint32_t machine_fingerprint(const std::string &normalized_mac)
{
    return crc32(reinterpret_cast<const uint8_t *>(normalized_mac.data()), normalized_mac.size());
}

uint32_t date_to_days(const std::string &d)
{
    if (d.size() < 10) return 0;
    int Y = std::atoi(d.substr(0, 4).c_str());
    int M = std::atoi(d.substr(5, 2).c_str());
    int D = std::atoi(d.substr(8, 2).c_str());
    if (Y < 1970 || M < 1 || M > 12 || D < 1 || D > 31) return 0;

    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon = M - 1;
    tm.tm_mday = D;
#if defined(_WIN32)
    std::time_t t = _mkgmtime(&tm);
#else
    std::time_t t = timegm(&tm);
#endif
    if (t < 0) return 0;
    return static_cast<uint32_t>(t / 86400);
}

std::string days_to_date(uint32_t days)
{
    std::time_t t = static_cast<std::time_t>(days) * 86400;
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::setfill('0') << std::setw(4) << (tm.tm_year + 1900) << '-'
       << std::setfill('0') << std::setw(2) << (tm.tm_mon + 1) << '-'
       << std::setfill('0') << std::setw(2) << tm.tm_mday;
    return os.str();
}

uint32_t today_days()
{
    return static_cast<uint32_t>(std::time(nullptr) / 86400);
}

std::string encode(const license_info &info, const std::string &mac)
{
    std::string norm = normalize_mac(mac);
    uint32_t fp = machine_fingerprint(norm);

    uint8_t buf[16];
    pack(buf, info, fp);

    char key[16];
    std::memcpy(key, SECRET_KEY, 16);

    CRegister cr;
    std::string in_str(reinterpret_cast<const char *>(buf), 16);
    std::string out_hex;
    cr.AES128Encrypt(in_str, key, out_hex);
    // AES128Encrypt 默认输出大写十六进制
    return out_hex;
}

bool decode(const std::string &license_str, const std::string &mac, license_info &out_info)
{
    if (license_str.size() != 32) return false;
    for (char ch : license_str)
    {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
    }

    std::string norm = normalize_mac(mac);
    uint32_t fp = machine_fingerprint(norm);

    char key[16];
    std::memcpy(key, SECRET_KEY, 16);

    CRegister cr;
    std::string plain;
    cr.AES128Decrypt(license_str, key, plain);
    if (plain.size() < 16) return false;

    uint8_t buf[16];
    std::memcpy(buf, plain.data(), 16);

    out_info.machine_id = norm;
    return unpack(buf, fp, out_info);
}
} // namespace license_codec
