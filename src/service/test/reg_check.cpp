/*
    reg_check — 校验本机许可

    用法:
        reg_check [license_file]

    若不指定 license_file，则仅打印本机识别到的 MAC 与机器码 (规范化 MAC + 指纹)。
    若指定 license_file，则尝试加载并打印所有许可字段。
*/
#include "license_check/license_check.h"
#include "license_check/license_codec.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");

    license_check chk;

    spdlog::info("Detected MACs:");
    for (const auto &m : license_check::get_all_macs())
        spdlog::info("  {}", m);

    spdlog::info("Primary MAC : {}", chk.display_mac());
    spdlog::info("Machine ID  : {}", chk.machine_id());
    spdlog::info("Fingerprint : 0x{:08X}", license_codec::machine_fingerprint(chk.machine_id()));

    if (argc < 2)
    {
        spdlog::info("No license file specified.");
        return 0;
    }

    int rc = chk.load_license_file(argv[1]);
    if (rc != 0)
    {
        spdlog::error("Load license failed (rc={}): {}", rc,
                      rc == -1   ? "open file failed"
                      : rc == -2 ? "empty content"
                      : rc == -3 ? "decode failed (bad code or MAC mismatch)"
                      : rc == -4 ? "license expired"
                                 : "unknown");
        return 1;
    }

    spdlog::info("===== License Active =====");
    spdlog::info("Expire Date : {}", chk.expire_date());
    spdlog::info("Remaining   : {} day(s)", chk.remaining_days());
    spdlog::info("Server Limit: {}", chk.server_limit() == 0 ? std::string("unlimited") : std::to_string(chk.server_limit()));
    spdlog::info("Client Limit: {}", chk.client_limit() == 0 ? std::string("unlimited") : std::to_string(chk.client_limit()));
    spdlog::info("Enable Pull : {}", chk.enable_pull());
    spdlog::info("Enable Push : {}", chk.enable_push());
    return 0;
}
