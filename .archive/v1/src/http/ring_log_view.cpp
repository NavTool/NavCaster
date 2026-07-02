#include "ring_log_view.h"

namespace
{
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> g_sink;
}

namespace ring_log_view
{
    spdlog::sink_ptr create_sink(size_t capacity)
    {
        if (!g_sink)
            g_sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(capacity);
        return g_sink;
    }

    std::vector<std::string> last_n(size_t n)
    {
        if (!g_sink)
            return {};
        if (n == 0)
            n = static_cast<size_t>(-1);
        return g_sink->last_formatted(n);
    }
}
