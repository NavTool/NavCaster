#include <iostream>
#include <fstream>
#include <memory>
// #include <coroutine>

#ifdef WIN32

#else
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

#ifdef _WIN32
#pragma comment(lib, "Iphlpapi.lib")
// 解决在宇宙编译器(Visual Studio)下 libevent evutil.c找不到符号的问题
#endif

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/hourly_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#if defined(__GNUC__)
// GCC 编译器相关的代码
#elif defined(_MSC_VER)
// Visual Studio 编译器相关的代码
// #define YAML_CPP_STATIC_DEFINE
#include <direct.h> // 包含 _chdir 函数的头文件
#elif defined(__clang__)
// Clang 编译器相关的代码
#else
// 其他编译器的代码
#endif

#include "yaml-cpp/yaml.h"

#include "version.h"
#include "ntrip_config.h"
#include "ntrip_caster.h"

#define CONF_PATH "conf/"

// trace：最详细的日志级别，提供追踪程序执行流程的信息。
// debug：调试级别的日志信息，用于调试程序逻辑和查找问题。
// info：通知级别的日志信息，提供程序运行时的一般信息。
// warn：警告级别的日志信息，表明可能发生错误或不符合预期的情况。
// error：错误级别的日志信息，表明发生了某些错误或异常情况。
// critical：严重错误级别的日志信息，表示一个致命的或不可恢复的错误。

int switch_Working_Dir(std::string exe_path)
{
    std::string exepath = exe_path;

    // 找到路径中最后一个斜杠的位置
    size_t lastSlashPos = exepath.find_last_of("/\\");
    if (lastSlashPos == std::string::npos)
    {
        spdlog::error("Unable to extract directory from executable path.");
        return 1;
    }
    // 提取路径
    std::string exeDir = exepath.substr(0, lastSlashPos);

    // 切换工作目录
#if defined(_MSC_VER)
    if (_chdir(exeDir.c_str()) != 0)
    {
        spdlog::error("Failed to change working directory.");
        return 1;
    }
#else
    if (chdir(exeDir.c_str()) != 0)
    {
        spdlog::error("Failed to change working directory.");
        return 1;
    }
#endif

    spdlog::info("Switch Working directory: {}", exeDir);
    return 0;
}

int main(int argc, char **argv)
{
#ifdef WIN32

#else
    // 启用dump
    prctl(PR_SET_DUMPABLE, 1);

    // 设置core dump大小
    struct rlimit rlimit_core;
    rlimit_core.rlim_cur = RLIM_INFINITY; // 设置大小为无限
    rlimit_core.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &rlimit_core);
#endif

    // 程序启动
    spdlog::info("Software: {}-{}", PROJECT_SET_NAME, PROJECT_SET_VERSION);
    spdlog::info("Tag Version: {}", PROJECT_TAG_VERSION);
    spdlog::info("Git Version: {}", PROJECT_GIT_VERSION);

    // 解析输入：
    std::string conf_path = CONF_PATH;
    int listen_port = -1;

    if (argc < 1)
    {
        return 1;
    }

    if (argc == 2)
    {
        if (!strcmp(argv[1], "-info")) // 监听端口
        {
            exit(0);
        }
    }
    if (argc > 2)
    {
        for (int i = 1; i < argc; i += 2)
        {
            if (!strcmp(argv[i], "-port")) // 监听端口
            {
                listen_port = atoi(argv[i + 1]);
                spdlog::info("set listen port: {}", listen_port);
            }
            else if (!strcmp(argv[i], "-conf")) // 配置文件路径
            {
                conf_path = argv[i + 1];
                spdlog::info("set conf path: {}", conf_path);
            }
        }
    }

    switch_Working_Dir(argv[0]); // 切换工作路径到可执行目录下

    // 打开配置文件
    spdlog::info("Conf Path:{}", conf_path);
    // 读取全局配置
    spdlog::info("Load Conf...");

    ntrip_config::getInstance()->load_Caster_Conf(conf_path + "Service_Setting.yml");
    ntrip_config::getInstance()->load_Core_Conf(conf_path + "Caster_Core.yml");
    ntrip_config::getInstance()->load_Auth_Conf(conf_path + "Auth_Verify.yml");

    if (listen_port > 0)
    {
        ntrip_config::getInstance()->_listener_opt.set_listen_port(listen_port);
    }

    // 日志输出选项
    bool log_to_std = ntrip_config::getInstance()->_service_opt.output_stdout();
    bool log_to_file = ntrip_config::getInstance()->_service_opt.output_file();
    bool log_file_daily = ntrip_config::getInstance()->_service_opt.output_file_daily();
    bool log_file_hourly = ntrip_config::getInstance()->_service_opt.output_file_hourly();
    bool log_file_rotate = ntrip_config::getInstance()->_service_opt.output_file_rotate();
    int log_rotating_size = ntrip_config::getInstance()->_service_opt.file_rotate_size();
    int log_rotating_quata = ntrip_config::getInstance()->_service_opt.file_rotate_quata();
    std::string log_save_path = ntrip_config::getInstance()->_service_opt.file_save_path();

    // 开发者模式相关
    bool Core_Dump = ntrip_config::getInstance()->_service_opt.output_stdout();
    bool log_debug = ntrip_config::getInstance()->_service_opt.output_stdout();

    if (!Core_Dump) // 是否需要关闭 coredump
    {
#ifdef WIN32

#else
        prctl(PR_SET_DUMPABLE, 0);
#endif
    }

    // 初始化日志系统
    std::vector<spdlog::sink_ptr> sinks;
    if (log_to_std) // 输出到控制台
    {
        spdlog::info("Write log to Std...");
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_st>());
    }
    if (log_to_file) // 输出到文件
    {
        if (log_file_daily)
        {
            spdlog::info("Write log to File Daily...");
            sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>(log_save_path, 0, 0));
        }
        if (log_file_hourly)
        {
            spdlog::info("Write log to File Hourly...");
            sinks.push_back(std::make_shared<spdlog::sinks::hourly_file_sink_st>(log_save_path));
        }
        if (log_file_rotate)
        {
            spdlog::info("Write log to File Rotate...");
            size_t max_file_size = log_rotating_size * 1024 * 1024; // 单个日志文件大小：X MB
            size_t max_files = log_rotating_quata;                  // 最多保留 X 个文件
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_st>(log_save_path, max_file_size, max_files));
        }
    }
    // 把所有sink放入logger
    auto logger = std::make_shared<spdlog::logger>("log", begin(sinks), end(sinks));
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info); // 立即刷新日志
    if (log_debug)                         // 设置输出日志的级别
    {
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
    }

    // 初始化完成，开始进入正式流程
    spdlog::info("Software: {}-{}", PROJECT_SET_NAME, PROJECT_SET_VERSION);
    spdlog::info("Tag Version: {}", PROJECT_TAG_VERSION);
    spdlog::info("Git Version: {}", PROJECT_GIT_VERSION);

#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        spdlog::info("WSAStartup failed! exit.");
        return 1;
    }
#endif

    // auto x = new ntrip_caster(cfg);
    // delete x;
    // x->start();

    // spdlog::info("Init Server...");

    // auto str1 = a._license_check.gen_register_file();
    // a._license_check.load_license_file();
    // a._license_check.fresh_license_file();

    spdlog::info("Start Server...");
    ntrip_caster::getInstance()->start();

#ifdef WIN32
    WSACleanup();
#endif

    spdlog::info("Exit!");
    return 0;
}
