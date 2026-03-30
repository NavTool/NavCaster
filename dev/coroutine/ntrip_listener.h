#pragma once

#include <event2/event.h>

struct DetachedTask
{
    struct promise_type
    {

        // get_return_object() 定义了当协程被调用时，应该返回什么对象。对于这个例子来说，我们不需要返回任何对象，所以返回一个空的 DetachedTask 对象。
        DetachedTask get_return_object() noexcept { return {}; }

        // 首次进入协程函数并挂起
        std::suspend_never initial_suspend() noexcept { return {}; }

        // 协程函数结束后挂起
        std::suspend_never final_suspend() noexcept { return {}; }

        // 处理协程的返回值，这里我们不需要返回任何值，所以直接定义一个空的函数。
        void return_void() noexcept {}

        // 处理协程内未捕获的异常，默认行为是调用 std::terminate()，我们可以覆盖这个函数来提供自定义的异常处理逻辑。
        void unhandled_exception() { std::terminate(); }
    };
};

class ntrip_listener
{
private:
    /* data */
public:
    ntrip_listener(/* args */);
    ~ntrip_listener();

    // 协程函数
    DetachedTask listener_flow(event_base *base)
    {
        // 监听新连接，处理请求等
        // auto fd = co_await AcceptAwaitable{8001, base};
        if (fd < 0)
            break;
        auto conn = make_connection_from_fd(base, fd);
        connection_event_loop(conn, base);
    }
};

ntrip_listener::ntrip_listener(/* args */)
{
}

ntrip_listener::~ntrip_listener()
{
}
