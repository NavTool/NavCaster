
#pragma once
//
// carrier_base.h
// 基站/移动站/中继/源表等所有 Carrier 的统一基类
//
// 设计理念（混合协程）：
//   初始化阶段：co_await co_auth_login / co_caster_register / co_subscribe 等
//               回调到达时恢复对应的挂起点（一次性）
//   running 阶段：所有回调统一投递到 EventChannel，协程用 co_await _events.next() 消费
//               从而能捕获 Auth 踢下线、Caster 踢下线、订阅数据、TCP 断连等
//

#include "ntrip_global.h"
#include "process_queue.h"

#include <coroutine>
#include <deque>
#include <memory>
#include <optional>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <spdlog/spdlog.h>

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
        void unhandled_exception()
        {
            try { std::rethrow_exception(std::current_exception()); }
            catch (const std::exception &e) { spdlog::critical("[DetachedTask]: unhandled exception: {}", e.what()); }
            catch (...) { spdlog::critical("[DetachedTask]: unhandled unknown exception"); }
            std::terminate();
        }
    };
};

// ============================================================================
// CarrierEvent: 所有事件的统一表示
// ============================================================================

enum class CarrierEventType
{
    BevRead,        // TCP 收到数据
    BevWrite,       // TCP 写完成
    BevEvent,       // Bev事件 （connect/timeout/event等情况)
    Timeout,        // 定时器触发
    AuthReply,      // 认证结果（running 阶段收到 = 踢下线通知）
    RegisterReply,  // 注册结果（running 阶段收到 = 踢下线通知）
    SubscribeReply, // 订阅数据 / 订阅状态变化
};

struct CarrierEvent
{
    CarrierEventType type;   // 事件的类型，根据这个类型来决定下一步的操作

    // BevRead 数据
    std::vector<uint8_t> data;

    // libevent 事件标志（BevEvent 时有效）
    short bev_events = 0;

    // Auth 回调结果
    ::AuthReply auth_type = ::AuthReply::ERR;

    // Caster 回调结果
    ::CasterReply caster_type = ::CasterReply::ERR;

};

// ============================================================================
// EventChannel: 统一事件通道
//   running 阶段的协程用 co_await next() 消费所有事件
// ============================================================================

class EventChannel
{
public:
    struct DispatchAwaitable
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

    DispatchAwaitable next() { return DispatchAwaitable{this}; }

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

// ============================================================================
// 自由函数声明
// ============================================================================

std::string build_nrtip_reply(ConnectType type, bool version2, bool chunked);
std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth);

bool verify_ntrip_response(const char *data, size_t len, bool &version2, bool &chunked);

// ============================================================================
// carrier_base: 所有 Carrier 的统一基类
// ============================================================================

class carrier_base : public std::enable_shared_from_this<carrier_base>
{
public:
    ConnectInfo _info;

    // 运行相关变量
    std::string _mount_point;            // 挂载点
    std::string _user_name;              // 用户名
    std::string _connect_key;            // 连接唯一标识，格式为 type:addr:port
    bool _ntrip_version2 = false;        // 回复消息按1.0还是2.0
    bool _transfer_with_chunked = false; // 数据传输是否使用chunk
    size_t _chunked_size = 0;

    std::string _subscribe_channel; // 当前正在订阅的频道

    // relay运行状态
    bool _connected = false; // 是否已连接到第三方服务器
    bool _reconnect = false; // 是否需要重连

    // 统一事件通道
    EventChannel _events;

protected:
    CasterRegisterType _register_type = CasterRegisterType::UNKNOWN; // 运行时注册类型状态
    AuthType _auth_type = AuthType::UNKNOWN;                         // 运行时认证类型状态

public:
    std::string __class__ = "carrier_base";

    bufferevent *_bev;

    evbuffer *_send_evbuf; // 发送缓冲区
    evbuffer *_recv_evbuf; // 接收缓冲区

    bool _timeout_ev_flag = false;
    event *_timeout_ev;
    timeval _timeout_tv;

    // 初始化阶段的挂起点和结果缓存
    std::coroutine_handle<> _pending_auth = nullptr;
    auth_reply _auth_result{};

    std::coroutine_handle<> _pending_register = nullptr;
    caster_reply _register_result{};

    std::coroutine_handle<> _pending_subscribe = nullptr;
    caster_reply _subscribe_result{};

    std::coroutine_handle<> _pending_bev_event = nullptr;
    short _bev_event_result = 0;

    std::coroutine_handle<> _pending_bev_read = nullptr;
    std::vector<uint8_t> _bev_read_result;

    std::coroutine_handle<> _pending_bev_write = nullptr; // 可写

public:
    carrier_base(ConnectInfo info);
    virtual ~carrier_base();

    // ============ 协程入口（派生类必须实现） ============
    virtual DetachedTask run() = 0;

    // ============ 生命周期 ============
    int init() { return 0; }

    int start();

    virtual int stop();


    // ============ 工具方法（通用操作，供派生类调用）============
public:
    std::string create_bev(std::string addr, int port);
    int destroy_bev(std::string connect_key);
    int start_bev(bool enable_read_cb, time_t read_timeout_sec, bool enable_write_cb, time_t write_timeout_sec);
    int stop_bev();

    int start_timeout_event(time_t timeout_sec);
    int stop_timeout_event();

    int auth_login(AuthType type);
    int auth_logout();

    int caster_register(CasterRegisterType type);
    int caster_withdraw();

    std::vector<uint8_t> read_data(bool chunked);
    int send_data(const char *data, size_t len, bool chunked);

    int publish_data(const char *data, size_t len);
    int subscribe();
    int subscribe(double lon, double lat);
    int unsubscribe();

private:
    std::vector<uint8_t> read_data_from_evbuf();
    std::vector<uint8_t> read_data_from_chunk();



    // ============ 回调路由：有挂起点则恢复（初始化），否则投递 EventChannel（running） ============

    int auth_login_cb(auth_reply *reply);
    int caster_register_cb(caster_reply *reply);
    int caster_subscribe_cb(caster_reply *reply);

    int bev_read_cb(struct bufferevent *bev);
    int bev_write_cb(struct bufferevent *bev);
    int bev_event_cb(struct bufferevent *bev, short events);

    int timeout_cb();

public:
    // 库的回调函数

    // bufferevent的回调函数
    static void BevReadCallback(struct bufferevent *bev, void *arg);
    static void BevWriteCallback(struct bufferevent *bev, void *arg);
    static void BevEventCallback(struct bufferevent *bev, short events, void *arg);

    // 定时器的回调函数
    static void TimeoutCallback(evutil_socket_t fd, short events, void *arg);

    // Auth的注册的回调函数
    static void AuthLoginCallback(const char *request, void *arg, auth_reply *reply);

    // Caster的回调函数
    static void CasterRegisterCallback(const char *request, void *arg, caster_reply *reply);
    static void CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply);

    // ============ 初始化阶段 Awaitables ============

    struct AuthRegisterAwaitable
    {
        carrier_base *self;
        AuthType type;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_auth = h;   // 记录挂起点
            self->auth_login(type);    // 调用Auth注册函数
        }
        auth_reply await_resume() noexcept { return self->_auth_result; }
    };

    struct CasterRegisterAwaitable
    {
        carrier_base *self;
        CasterRegisterType type;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_register = h;
            self->caster_register(type);
        }
        caster_reply await_resume() noexcept { return self->_register_result; }
    };

    struct CasterSubscribeAwaitable
    {
        carrier_base *self;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_subscribe = h;
            self->subscribe();
        }
        caster_reply await_resume() noexcept { return self->_subscribe_result; }
    };

    struct BevEventAwaitable
    {
        carrier_base *self;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_bev_event = h;
        }
        short await_resume() noexcept { return self->_bev_event_result; }
    };

    struct BevReadAwaitable
    {
        carrier_base *self;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_bev_read = h;
        }
        std::vector<uint8_t> await_resume() noexcept
        {
            return std::move(self->_bev_read_result);
        }
    };

    struct BevWriteAwaitable
    {
        carrier_base *self;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_bev_write = h;
        }
        bool await_resume() noexcept
        {
            return true;
        }
    };


protected:
    // ============ 协程调用接口 ============

    AuthRegisterAwaitable co_auth_login(AuthType type);  // 注册到Auth的协程函数

    CasterRegisterAwaitable co_caster_register(CasterRegisterType type);

    CasterSubscribeAwaitable co_caster_subscribe();

    BevEventAwaitable co_wait_bev_event();

    BevReadAwaitable co_wait_bev_read();

    BevWriteAwaitable co_wait_bev_write();

    TimerAwaitable co_sleep(time_t sec);
};
