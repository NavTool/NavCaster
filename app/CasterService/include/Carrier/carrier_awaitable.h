#pragma once
//
// carrier_awaitable.h
// C++20 协程适配层 — 混合设计
//
// 设计理念：
//   1. 初始化阶段使用 co_await 顺序执行（逻辑清晰）
//   2. 每步执行后，回调自动"注册"到 EventChannel
//   3. running 阶段用 co_await _events.next() 统一消费所有事件
//

#include <coroutine>
#include <deque>
#include <vector>
#include <optional>

#include "carrier_base.h"

// ============================================================================
// DetachedTask: fire-and-forget 协程返回类型
// ============================================================================
struct DetachedTask
{
    struct promise_type
    {
        DetachedTask get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
};

// ============================================================================
// CarrierEvent: 所有事件的统一表示
// ============================================================================

enum class CarrierEventType
{
    BevRead,       // TCP 收到数据
    BevWrite,      // TCP 写完成
    BevConnected,  // TCP 连接建立（relay 场景）
    BevDisconnect, // TCP 连接断开 / 错误
    Timeout,       // 定时器触发
    AuthReply,     // 认证结果（running 阶段收到 = 踢下线通知）
    RegisterReply, // 注册结果（running 阶段收到 = 踢下线通知）
    SubscribeReply,// 订阅数据 / 订阅状态变化
};

struct CarrierEvent
{
    CarrierEventType type;

    // BevRead 数据
    std::vector<uint8_t> data;

    // libevent 事件标志（BevConnected / BevDisconnect）
    short bev_events = 0;

    // Auth 回调结果
    ::AuthReply auth_type = ::AuthReply::ERR;

    // Caster 回调结果
    ::CasterReply caster_type = ::CasterReply::ERR;
    std::vector<char> caster_data;
};

// ============================================================================
// EventChannel: 统一事件通道
//   running 阶段的协程用 co_await next() 消费所有事件
// ============================================================================

class EventChannel
{
public:
    struct ReadAwaitable
    {
        EventChannel *ch;

        bool await_ready() const noexcept
        {
            return !ch->_queue.empty() || ch->_closed;
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            ch->_waiter = h;
        }

        std::optional<CarrierEvent> await_resume() noexcept
        {
            if (ch->_queue.empty())
                return std::nullopt;
            auto evt = std::move(ch->_queue.front());
            ch->_queue.pop_front();
            return evt;
        }
    };

    ReadAwaitable next() { return ReadAwaitable{this}; }

    void push(CarrierEvent evt)
    {
        _queue.push_back(std::move(evt));
        if (_waiter)
        {
            auto h = _waiter;
            _waiter = nullptr;
            h.resume();
        }
    }

    void close()
    {
        _closed = true;
        if (_waiter)
        {
            auto h = _waiter;
            _waiter = nullptr;
            h.resume();
        }
    }

    void reset()
    {
        _queue.clear();
        _closed = false;
        _waiter = nullptr;
    }

    bool closed() const { return _closed; }

private:
    std::deque<CarrierEvent> _queue;
    std::coroutine_handle<> _waiter = nullptr;
    bool _closed = false;
};

// ============================================================================
// TimerAwaitable: co_await 定时器
// ============================================================================
struct TimerAwaitable
{
    event_base *base;
    timeval tv;

    TimerAwaitable(event_base *b, time_t sec, long usec = 0) : base(b)
    {
        tv.tv_sec = sec;
        tv.tv_usec = usec;
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h)
    {
        struct State
        {
            std::coroutine_handle<> handle;
            event *ev;
        };
        auto *s = new State{h, nullptr};
        s->ev = event_new(base, -1, 0,
                          [](evutil_socket_t, short, void *arg)
                          {
                              auto *st = static_cast<State *>(arg);
                              if (st->ev)
                                  event_free(st->ev);
                              auto hdl = st->handle;
                              delete st;
                              hdl.resume();
                          },
                          s);
        event_add(s->ev, &tv);
    }

    void await_resume() const noexcept {}
};
