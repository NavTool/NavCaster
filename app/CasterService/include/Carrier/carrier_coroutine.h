#pragma once
//
// carrier_coroutine.h
// 协程模式的 carrier 基类 — 混合设计
//
// 设计理念：
//   初始化阶段：co_await co_auth_login / co_caster_register / co_subscribe 等
//               回调到达时恢复对应的挂起点（一次性）
//   running 阶段：所有回调统一投递到 EventChannel，协程用 co_await _events.next() 消费
//               从而能捕获 Auth 踢下线、Caster 踢下线、订阅数据、TCP 断连等
//

#include "carrier_awaitable.h"

class carrier_coroutine : public carrier_base
{
public:
    EventChannel _events; // 统一事件通道

    // ============ 初始化阶段的挂起点和结果缓存 ============
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

    carrier_coroutine(ConnectInfo info) : carrier_base(info)
    {
        __class__ = "carrier_coroutine";
    }

    virtual ~carrier_coroutine() = default;

    // ============ 派生类需要实现的协程入口 ============
    virtual DetachedTask run() = 0;

    // ============ 生命周期 ============
    int init() override { return 0; }

    int start() override
    {
        run(); // 启动协程（fire-and-forget）
        return 0;
    }

    int stop() override
    {
        stop_bev();
        stop_timeout_event();
        auth_logout();
        unsubscribe();
        caster_withdraw();

        _events.close();

        _info.set_operate(OPERATE_TYPE_DESTORY);
        QUEUE::Push(_info);

        spdlog::info("[{}]: stopped, mount [{}], addr:[{}:{}]",
                     __class__, _info.mount_point(), _info.addr(), _info.port());
        return 0;
    }

    // ============ 回调路由：有挂起点则恢复（初始化），否则投递 EventChannel（running） ============

    int login_cb(auth_reply *reply) override
    {
        if (_pending_auth)
        {
            _auth_result = *reply;
            auto h = _pending_auth;
            _pending_auth = nullptr;
            h.resume();
        }
        else
        {
            CarrierEvent evt;
            evt.type = CarrierEventType::AuthReply;
            evt.auth_type = reply->type;
            _events.push(std::move(evt));
        }
        return 0;
    }

    int register_cb(caster_reply *reply) override
    {
        if (_pending_register)
        {
            _register_result = *reply;
            auto h = _pending_register;
            _pending_register = nullptr;
            h.resume();
        }
        else
        {
            CarrierEvent evt;
            evt.type = CarrierEventType::RegisterReply;
            evt.caster_type = reply->type;
            if (reply->str && reply->len > 0)
                evt.caster_data.assign(reply->str, reply->str + reply->len);
            _events.push(std::move(evt));
        }
        return 0;
    }

    int subscribe_cb(caster_reply *reply) override
    {
        if (_pending_subscribe)
        {
            _subscribe_result = *reply;
            auto h = _pending_subscribe;
            _pending_subscribe = nullptr;
            h.resume();
        }
        else
        {
            CarrierEvent evt;
            evt.type = CarrierEventType::SubscribeReply;
            evt.caster_type = reply->type;
            if (reply->str && reply->len > 0)
                evt.caster_data.assign(reply->str, reply->str + reply->len);
            _events.push(std::move(evt));
        }
        return 0;
    }

    int read_cb(struct bufferevent *bev) override
    {
        if (_pending_bev_read)
        {
            _bev_read_result = read_data(false); // 初始化阶段（握手）不使用 chunked
            auto h = _pending_bev_read;
            _pending_bev_read = nullptr;
            h.resume();
        }
        else
        {
            CarrierEvent evt;
            evt.type = CarrierEventType::BevRead;
            evt.data = read_data(_transfer_with_chunked);
            _events.push(std::move(evt));
        }
        return 0;
    }

    int write_cb(struct bufferevent *bev) override
    {
        CarrierEvent evt;
        evt.type = CarrierEventType::BevWrite;
        _events.push(std::move(evt));
        return 0;
    }

    int event_cb(struct bufferevent *bev, short events) override
    {
        if (_pending_bev_event)
        {
            _bev_event_result = events;
            auto h = _pending_bev_event;
            _pending_bev_event = nullptr;
            h.resume();
        }
        else
        {
            CarrierEvent evt;
            evt.bev_events = events;
            evt.type = (events & BEV_EVENT_CONNECTED)
                           ? CarrierEventType::BevConnected
                           : CarrierEventType::BevDisconnect;
            _events.push(std::move(evt));
        }
        return 0;
    }

    int timeout_cb() override
    {
        CarrierEvent evt;
        evt.type = CarrierEventType::Timeout;
        _events.push(std::move(evt));
        return 0;
    }

    // ============ 初始化阶段 Awaitables（轻量包装，走 carrier_base 标准回调路径） ============

    struct InitAuthAwaitable
    {
        carrier_coroutine *self;
        AuthType type;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_auth = h;
            self->auth_login(type);
        }
        auth_reply await_resume() noexcept { return self->_auth_result; }
    };

    struct InitRegisterAwaitable
    {
        carrier_coroutine *self;
        CasterRegisterType type;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_register = h;
            self->caster_register(type);
        }
        caster_reply await_resume() noexcept { return self->_register_result; }
    };

    struct InitSubscribeAwaitable
    {
        carrier_coroutine *self;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_subscribe = h;
            self->subscribe();
        }
        caster_reply await_resume() noexcept { return self->_subscribe_result; }
    };

    struct InitBevEventAwaitable
    {
        carrier_coroutine *self;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h)
        {
            self->_pending_bev_event = h;
        }
        short await_resume() noexcept { return self->_bev_event_result; }
    };

    struct InitBevReadAwaitable
    {
        carrier_coroutine *self;

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

protected:
    // ============ 协程便利方法 ============

    InitAuthAwaitable co_auth_login(AuthType type)
    {
        _auth_type = type;
        return InitAuthAwaitable{this, type};
    }

    InitRegisterAwaitable co_caster_register(CasterRegisterType type)
    {
        _register_type = type;
        return InitRegisterAwaitable{this, type};
    }

    InitSubscribeAwaitable co_subscribe()
    {
        return InitSubscribeAwaitable{this};
    }

    InitBevEventAwaitable co_wait_bev_event()
    {
        return InitBevEventAwaitable{this};
    }

    InitBevReadAwaitable co_wait_bev_read()
    {
        return InitBevReadAwaitable{this};
    }

    TimerAwaitable co_sleep(time_t sec)
    {
        return TimerAwaitable(connect_bev::getInstance()->get_base(), sec);
    }
};
