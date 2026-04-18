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

#include "log_ring_buffer.h"
#include "version.h"
#include "ntrip_config.h"
#include "ntrip_caster.h"

#include <csignal>

#define CONF_PATH "conf/"

// trace：最详细的日志级别，提供追踪程序执行流程的信息。
// debug：调试级别的日志信息，用于调试程序逻辑和查找问题。
// info：通知级别的日志信息，提供程序运行时的一般信息。
// warn：警告级别的日志信息，表明可能发生错误或不符合预期的情况。
// error：错误级别的日志信息，表明发生了某些错误或异常情况。
// critical：严重错误级别的日志信息，表示一个致命的或不可恢复的错误。

int init_core_dump()
{
    // 开发者模式相关
    bool Core_Dump = ntrip_config::getInstance()->_service_opt.output_stdout();
#ifdef WIN32

#else
    // 启用dump
    prctl(PR_SET_DUMPABLE, 1);

    // 设置core dump大小
    struct rlimit rlimit_core;
    rlimit_core.rlim_cur = RLIM_INFINITY; // 设置大小为无限
    rlimit_core.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &rlimit_core);

    if (!Core_Dump) // 是否需要关闭 coredump
    {
        prctl(PR_SET_DUMPABLE, 0);
    }
#endif

    return 0;
}
int init_log_system()
{

    // 日志输出选项
    bool log_to_std = ntrip_config::getInstance()->_service_opt.output_stdout();
    bool log_to_file = ntrip_config::getInstance()->_service_opt.output_file();
    bool log_file_daily = ntrip_config::getInstance()->_service_opt.output_file_daily();
    bool log_file_hourly = ntrip_config::getInstance()->_service_opt.output_file_hourly();
    bool log_file_rotate = ntrip_config::getInstance()->_service_opt.output_file_rotate();
    int log_rotating_size = ntrip_config::getInstance()->_service_opt.file_rotate_size();
    int log_rotating_quata = ntrip_config::getInstance()->_service_opt.file_rotate_quata();
    std::string log_save_path = ntrip_config::getInstance()->_service_opt.file_save_path();

    bool log_debug = ntrip_config::getInstance()->_service_opt.output_debug_info();

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
    navcaster_log::install_ring_buffer_sink(500);
    spdlog::flush_on(spdlog::level::info); // 立即刷新日志
    if (log_debug)                         // 设置输出日志的级别
    {
        spdlog::set_level(spdlog::level::debug);
        spdlog::flush_on(spdlog::level::debug);
    }
    return 0;
}

int main(int argc, char **argv)
{
    // 解析传入的参数

    // 读取配置文件（从数据库/从配置文件）

    // 创建Caster实例

    // 依次启动服务
    // 初始化日志系统（配置日志的输出形式和日志级别）
    // 初始化鉴权中心连接
    // 初始化Caster连接
    // 初始化消息队列
    // 启动监听器，开始接受连接请求
    // 连接请求处理->生成连接信息->鉴权->鉴权成功后放入消息队列
    // 触发任务处理->从消息队列中取出连接信息->根据连接信息创建连接->连接成功后进行数据转发
    // 连接断开/异常->创建关闭连接请求->放入消息队列
    // 触发任务处理->从消息队列中取出关闭连接请求->根据连接信息关闭连接->进行资源清理

    // 程序启动
    spdlog::info("Software: {}-{}", PROJECT_SET_NAME, PROJECT_SET_VERSION);
    spdlog::info("Tag Version: {}", PROJECT_TAG_VERSION);
    spdlog::info("Git Version: {}", PROJECT_GIT_VERSION);

    // 传入配置获取地址，从远程配置中心获取配置后启动
    ntrip_config::getInstance()->Init(argc, argv, CONF_PATH); // 初始化配置

    init_core_dump(); // 初始化coredump

    init_log_system(); // 初始化日志系统

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

    spdlog::info("Start Server...");

    // 信号处理: 捕获SIGTERM/SIGINT用于优雅停机
    auto signal_handler = [](int sig)
    {
        spdlog::info("Received signal {}, shutting down...", sig);
        ntrip_caster::getInstance()->stop();
    };
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    ntrip_caster::getInstance()->start();

#ifdef WIN32
    WSACleanup();
#endif

    spdlog::info("Exit!");
    return 0;
}
