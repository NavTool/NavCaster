/*
    reg_gen — 注册机 (License 生成器)

    用法:
        reg_gen --machine <MAC> [--servers N] [--clients N]
                [--pull] [--push] [--expire YYYY-MM-DD]
                [-o <output_file>]

    参数:
        --machine    设备 MAC 地址 (任意分隔符，会规范化)
        --servers    基站上限, 默认 0 (不限制)
        --clients    用户上限, 默认 0 (不限制)
        --pull       允许 pull 转发 (默认禁用)
        --push       允许 push 转发 (默认禁用)
        --expire     有效期截止日期, YYYY-MM-DD; 不指定表示永不过期
        -o           许可码输出文件; 不指定则仅打印到 stdout

    示例:
        reg_gen --machine aa:bb:cc:dd:ee:ff --servers 50 --clients 1000 \
                --pull --push --expire 2026-12-31 -o license.lic
*/
#include "license_check/license_codec.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace
{
void print_usage(const char *prog)
{
    std::printf(
        "Usage: %s --machine <MAC> [--servers N] [--clients N]\n"
        "         [--pull] [--push] [--expire YYYY-MM-DD] [-o <file>]\n",
        prog);
}

bool parse_int(const char *s, int &out)
{
    char *end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (!end || *end != '\0' || v < 0 || v > 0xFFFF)
        return false;
    out = static_cast<int>(v);
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    std::string mac;
    std::string expire;
    std::string output;
    license_info info;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto need_value = [&](const char *name) -> const char * {
            if (i + 1 >= argc)
            {
                spdlog::error("missing value for {}", name);
                std::exit(1);
            }
            return argv[++i];
        };

        if (a == "--machine") { mac = need_value("--machine"); }
        else if (a == "--servers")
        {
            if (!parse_int(need_value("--servers"), info.server_limit))
            {
                spdlog::error("invalid --servers (0..65535)"); return 1;
            }
        }
        else if (a == "--clients")
        {
            if (!parse_int(need_value("--clients"), info.client_limit))
            {
                spdlog::error("invalid --clients (0..65535)"); return 1;
            }
        }
        else if (a == "--pull")   { info.enable_pull = true; }
        else if (a == "--push")   { info.enable_push = true; }
        else if (a == "--expire") { expire = need_value("--expire"); }
        else if (a == "-o")       { output = need_value("-o"); }
        else if (a == "-h" || a == "--help")
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            spdlog::error("unknown argument: {}", a);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (mac.empty())
    {
        spdlog::error("--machine is required");
        print_usage(argv[0]);
        return 1;
    }

    if (!expire.empty())
    {
        info.expire_days = license_codec::date_to_days(expire);
        if (info.expire_days == 0)
        {
            spdlog::error("invalid --expire date (use YYYY-MM-DD, >= 1970-01-01)");
            return 1;
        }
    }

    std::string norm = license_codec::normalize_mac(mac);
    if (norm.size() != 12)
    {
        spdlog::error("MAC must contain 12 hex digits, got '{}'", norm);
        return 1;
    }

    std::string lic = license_codec::encode(info, mac);

    spdlog::info("Machine MAC : {}", mac);
    spdlog::info("Normalized  : {}", norm);
    spdlog::info("Fingerprint : 0x{:08X}", license_codec::machine_fingerprint(norm));
    spdlog::info("Expire      : {}", info.expire_days == 0 ? std::string("never")
                                                           : license_codec::days_to_date(info.expire_days));
    spdlog::info("Servers     : {}", info.server_limit == 0 ? std::string("unlimited")
                                                            : std::to_string(info.server_limit));
    spdlog::info("Clients     : {}", info.client_limit == 0 ? std::string("unlimited")
                                                            : std::to_string(info.client_limit));
    spdlog::info("Pull        : {}", info.enable_pull);
    spdlog::info("Push        : {}", info.enable_push);
    spdlog::info("License     : {}", lic);

    if (!output.empty())
    {
        std::ofstream of(output);
        if (!of)
        {
            spdlog::error("cannot open output file: {}", output);
            return 1;
        }
        of << lic << std::endl;
        spdlog::info("written to {}", output);
    }

    return 0;
}
