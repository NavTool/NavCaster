#ifndef CONNECT_BEV_H
#define CONNECT_BEV_H

#include <coroutine>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <stdexcept>
#include <string>
#include <iostream>

// 定义事件类型
enum class EventType {
    Read,
    Write,
    Timeout,
    Error,
    Closed
};

// 协程类封装 bufferevent
class BuffereventCoroutine {
public:
    BuffereventCoroutine(event_base* base, evutil_socket_t fd)
        : base_(base), bev_(bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE)) {
        if (!bev_) {
            throw std::runtime_error("Failed to create bufferevent");
        }
    }

    ~BuffereventCoroutine() {
        if (bev_) {
            bufferevent_free(bev_);
        }
    }

    // Awaitable 对象
    class Awaitable {
    public:
        Awaitable(bufferevent* bev, EventType eventType)
            : bev_(bev), eventType_(eventType) {}

        bool await_ready() const noexcept {
            return false; // 始终挂起
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            // 设置回调函数
            bufferevent_setcb(bev_,
                [](bufferevent* bev, void* ctx) {
                    auto* self = static_cast<Awaitable*>(ctx);
                    self->handle_ = nullptr; // 清空协程句柄
                    self->eventTriggered_ = true;
                    self->eventType_ = EventType::Read; // 假设触发了 Read 事件
                    self->resume();
                },
                nullptr,
                [](bufferevent* bev, short events, void* ctx) {
                    auto* self = static_cast<Awaitable*>(ctx);
                    self->handle_ = nullptr;
                    self->eventTriggered_ = true;
                    if (events & BEV_EVENT_EOF) {
                        self->eventType_ = EventType::Closed;
                    } else if (events & BEV_EVENT_ERROR) {
                        self->eventType_ = EventType::Error;
                    }
                    self->resume();
                },
                this);

            // 挂起协程
            handle_ = h;
        }

        EventType await_resume() noexcept {
            return eventType_;
        }

    private:
        void resume() {
            if (handle_) {
                handle_.resume();
            }
        }

        bufferevent* bev_;
        std::coroutine_handle<> handle_;
        EventType eventType_;
        bool eventTriggered_ = false;
    };

    // 等待事件
    Awaitable wait(EventType eventType) {
        return Awaitable(bev_, eventType);
    }

    // 发送数据
    void send(const std::string& data) {
        bufferevent_write(bev_, data.data(), data.size());
    }

private:
    event_base* base_;
    bufferevent* bev_;
};

#endif // CONNECT_BEV_H