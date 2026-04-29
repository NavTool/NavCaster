/*
    license_check.h — 运行时许可校验

    职责：
    1. 探测本机所有 MAC 地址，挑选主网卡 MAC 作为机器标识。
    2. 加载许可文件并使用 license_codec 解码 / 校验机器指纹与有效期。
    3. 对外提供 active / 各类上限 / 功能开关 / 剩余天数等访问器。

    若未提供许可文件或解码失败，license_check 处于未激活状态：
        active() == false
        server_limit/client_limit == 0 (调用者按需判断为不限制还是禁止)
        enable_pull/enable_push == false
*/
#pragma once

#include "license_codec.h"

#include <string>
#include <vector>

class license_check
{
public:
    license_check();
    ~license_check() = default;

    // 加载许可文件并立即校验; 失败时 active() 为 false
    int load_license_file(const std::string &license_file_path = "license.lic");

    // 写出当前主 MAC 到 register 文件 (供注册时上报给授权方)
    int write_register_file(const std::string &register_file_path = "register.lic");

    // 当前主 MAC (规范化后, 全小写无分隔符)
    const std::string &machine_id() const { return _machine_id; }
    // 显示用 MAC (xx:xx:xx:xx:xx:xx)
    const std::string &display_mac() const { return _primary_mac; }

    bool active() const { return _active; }
    bool is_expired() const;
    int  remaining_days() const;

    int  server_limit() const { return _info.server_limit; }
    int  client_limit() const { return _info.client_limit; }
    bool enable_pull() const { return _info.enable_pull; }
    bool enable_push() const { return _info.enable_push; }

    uint32_t    expire_days() const { return _info.expire_days; }
    std::string expire_date() const;

    const license_info &info() const { return _info; }

    // 列出本机所有 MAC (调试 / 注册码生成场景使用)
    static std::vector<std::string> get_all_macs();
    // 选择主 MAC: 第一个非全零的 MAC, 若全无返回 "00:00:00:00:00:00"
    static std::string pick_primary_mac();

private:
    std::string  _machine_id;     // 规范化主 MAC
    std::string  _primary_mac;    // 含分隔符的主 MAC 显示
    license_info _info;
    bool         _active = false;
};
