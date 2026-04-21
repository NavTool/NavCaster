#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <memory>
#include <string>
#include <vector>

// 进程内环形日志视图：
//   1. 在 main.cpp 里用 ring_log_view::create_sink(N) 创建 sink 并注入 sinks 列表；
//   2. HTTP API /api/logs/ring 通过 ring_log_view::last_n() 读取最近 N 条已渲染日志。
namespace ring_log_view
{
    // 创建并保存全局环形日志 sink (5000 条足够 1~2 分钟峰值)。
    // 重复调用返回首次创建的实例。
    spdlog::sink_ptr create_sink(size_t capacity = 5000);

    // 返回最近 n 条日志文本 (按时间升序)。n<=0 表示返回全部。
    std::vector<std::string> last_n(size_t n);
}
