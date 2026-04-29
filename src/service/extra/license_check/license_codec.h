/*
    license_codec.h — 注册码编解码模块

    设计目标：以单个 AES-128-ECB 块 (16 字节) 编码全部许可信息，
    并与设备 MAC 绑定，输出 32 位十六进制字符串作为许可码。

    Payload 布局 (16 bytes):
        offset 0..1   : magic "NC"
        offset 2      : version (currently 0x01)
        offset 3      : flags    (bit0 = enable_pull, bit1 = enable_push)
        offset 4..7   : machine fingerprint (uint32 LE, CRC32 of normalized MAC)
        offset 8..11  : expire_unix_days     (uint32 LE, 自 1970-01-01 UTC 起天数; 0 = 永不过期)
        offset 12..13 : server_limit         (uint16 LE; 0 = 不限制)
        offset 14..15 : client_limit         (uint16 LE; 0 = 不限制)
*/
#pragma once

#include <cstdint>
#include <string>

struct license_info
{
    int      server_limit = 0;     // 基站上限, 0 = 不限制
    int      client_limit = 0;     // 用户上限, 0 = 不限制
    bool     enable_pull  = false; // 是否启用 PULL 转发
    bool     enable_push  = false; // 是否启用 PUSH 转发
    uint32_t expire_days  = 0;     // 自 1970-01-01 UTC 起天数, 0 = 永不过期
    std::string machine_id;        // 解码后回填: 规范化 MAC
};

namespace license_codec
{
// 规范化 MAC 字符串: 仅保留十六进制字符并转小写
//   "AA:BB:CC:DD:EE:FF" -> "aabbccddeeff"
std::string normalize_mac(const std::string &mac);

// 由规范化 MAC 计算 32 位指纹 (CRC32)
uint32_t machine_fingerprint(const std::string &normalized_mac);

// 日期与天数转换 (UTC, YYYY-MM-DD)
//   解析失败返回 0
uint32_t    date_to_days(const std::string &yyyy_mm_dd);
std::string days_to_date(uint32_t days);

// 当前 UTC 天数
uint32_t today_days();

// 编码: 生成 32 位大写十六进制许可码
std::string encode(const license_info &info, const std::string &mac);

// 解码: 校验 magic / version / 机器指纹, 成功返回 true 并填充 out
bool decode(const std::string &license_str, const std::string &mac, license_info &out);
} // namespace license_codec
