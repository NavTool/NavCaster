#pragma once
#include "EventOperationBase.h"
#include <event2/event.h>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <async.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <unistd.h>
#endif

class EventWorker {
public:
    EventWorker();
    ~EventWorker();

    void start();
    void stop();

    // 普通事件任务
    QString postTask(std::shared_ptr<EventOperationBase> op);
    QString postTask(QString id,const std::function<void(event_base*)>& fn);
    void cancelTask(QString id);

    // Redis 异步任务
    QString postRedisTask(std::shared_ptr<RedisOperationBase> op);
    QString postRedisTask(QString id,const std::function<void(redisAsyncContext*)>& fn);
    void cancelRedisTask(QString id);

    // 定时任务
    QString addTimer(QString id,int intervalMs, std::function<void()> fn, bool repeat);
    void cancelTimer(QString id);

    event_base* base() const { return m_base; }
    redisAsyncContext* redisCtx() const { return m_redisCtx; }
    void setRedisCtx(redisAsyncContext* redisCtx){ m_redisCtx = redisCtx;};

private:
    struct TaskEntry {
        QString id;
        std::function<void(event_base*)> fn;
    };
    struct RedisTaskEntry {
        QString id;
        std::function<void(redisAsyncContext*)> fn;
    };
    struct TimerEntry {
        QString id;
        event* ev;
        std::function<void()> fn;
    };

private:
    static void onWakeup(evutil_socket_t fd, short, void* arg);
    static void onTimer(evutil_socket_t, short, void* arg);

    void threadMain();
    void wakeup();

private:
    event_base* m_base = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    evutil_socket_t m_notifyFds[2];
    event* m_notifyEvent = nullptr;

    std::queue<TaskEntry> m_taskQueue;
    std::mutex m_taskMutex;

    std::queue<RedisTaskEntry> m_redisTaskQueue;
    std::mutex m_redisTaskMutex;

    std::unordered_map<QString, TimerEntry*> m_timers;
    std::mutex m_timerMutex;


    // Redis
    redisAsyncContext* m_redisCtx = nullptr;
};
