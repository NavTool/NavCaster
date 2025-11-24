#include "EventWorker.h"
#include "adapters/libevent.h"
#include "async.h"
#include <iostream>

EventWorker::EventWorker()
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);
#endif

    if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, m_notifyFds) < 0)
        perror("socketpair failed");

    evutil_make_socket_nonblocking(m_notifyFds[0]);
    evutil_make_socket_nonblocking(m_notifyFds[1]);
}

EventWorker::~EventWorker()
{
    stop();

#ifdef _WIN32
    closesocket(m_notifyFds[0]);
    closesocket(m_notifyFds[1]);
    WSACleanup();
#else
    close(m_notifyFds[0]);
    close(m_notifyFds[1]);
#endif
}

void EventWorker::start()
{
    if (m_running.load()) return;
    m_running = true;
    m_thread = std::thread(&EventWorker::threadMain, this);
}

void EventWorker::stop()
{
    if (!m_running.load()) return;
    m_running = false;
    wakeup();

    if (m_thread.joinable())
        m_thread.join();

    // if (m_redisCtx) {
    //     redisAsyncDisconnect(m_redisCtx);
    //     m_redisCtx = nullptr;
    // }
}

QString EventWorker::postTask(std::shared_ptr<EventOperationBase> op)
{
    return  postTask(op->id(),
                    [op](event_base* base)
                    {
                        if (base) op->execute(base);
                    }
                    );
}

QString EventWorker::postTask(QString id, const std::function<void (event_base *)> &fn)
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    m_taskQueue.push({id, fn});
    wakeup();
    return id;
}


void EventWorker::cancelTask(QString id)
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    std::queue<TaskEntry> tmp;
    while (!m_taskQueue.empty()) {
        auto e = m_taskQueue.front();
        m_taskQueue.pop();
        if (e.id != id) tmp.push(e);
    }
    std::swap(m_taskQueue, tmp);
}

QString EventWorker::postRedisTask(std::shared_ptr<RedisOperationBase> op)
{
    return  postRedisTask(op->id(),
                         [op](redisAsyncContext* ctx)
                         {
                             if (ctx) op->execute(ctx);
                         }
                         );
}

QString EventWorker::postRedisTask(QString id, const std::function<void (redisAsyncContext *)> &fn)
{
    if(m_redisCtx==nullptr)
    {
        return QString();
    }
    std::lock_guard<std::mutex> lock(m_redisTaskMutex);
    m_redisTaskQueue.push({id, fn});
    wakeup();
    return id;
}

void EventWorker::cancelRedisTask(QString id)
{
    std::lock_guard<std::mutex> lock(m_redisTaskMutex);
    std::queue<RedisTaskEntry> tmp;
    while (!m_redisTaskQueue.empty()) {
        auto e = m_redisTaskQueue.front();
        m_redisTaskQueue.pop();
        if (e.id != id) tmp.push(e);
    }
    std::swap(m_redisTaskQueue, tmp);
}



// ---------------------- 定时器 ------------------------
QString EventWorker::addTimer(QString id, int intervalMs, std::function<void ()> fn, bool repeat)
{
    postTask(id,[this, id, intervalMs, fn, repeat](event_base* base){
        timeval tv{ intervalMs/1000, (intervalMs%1000)*1000 };
        TimerEntry* entry = new TimerEntry{ id, nullptr, fn };

        entry->ev = event_new(base, -1, (repeat ? (EV_PERSIST | EV_TIMEOUT) : EV_TIMEOUT),
                              &EventWorker::onTimer, entry);

        {
            std::lock_guard<std::mutex> lock(m_timerMutex);
            m_timers[id] = entry;
        }

        event_add(entry->ev, &tv);
    });

    return id;
}

void EventWorker::cancelTimer(QString id)
{
    std::lock_guard<std::mutex> lock(m_timerMutex);
    auto it = m_timers.find(id);
    if (it == m_timers.end()) return;

    TimerEntry* entry = it->second;
    event_del(entry->ev);
    event_free(entry->ev);
    delete entry;

    m_timers.erase(it);
}

// ---------------------- 线程与唤醒 ------------------------

void EventWorker::threadMain()
{
    m_base = event_base_new();

    // 创建通知事件
    m_notifyEvent = event_new(m_base, m_notifyFds[1], EV_READ | EV_PERSIST, &EventWorker::onWakeup, this);
    event_add(m_notifyEvent, nullptr);

    event_base_dispatch(m_base);

    // 清理
    event_free(m_notifyEvent);
    event_base_free(m_base);
    m_base = nullptr;
}

void EventWorker::wakeup()
{
    uint8_t b = 1;
#ifdef _WIN32
    send(m_notifyFds[0], (const char*)&b, 1, 0);
#else
    ::send(m_notifyFds[0], &b, 1, 0);
#endif
}

// ---------------------- 静态回调 ------------------------

void EventWorker::onWakeup(evutil_socket_t fd, short, void* arg)
{
    EventWorker* self = static_cast<EventWorker*>(arg);
    uint8_t buf[16];
#ifdef _WIN32
    recv(fd, (char*)buf, sizeof(buf), 0);
#else
    recv(fd, buf, sizeof(buf), 0);
#endif

    // 普通任务
    std::queue<TaskEntry> tasks;
    {
        std::lock_guard<std::mutex> lock(self->m_taskMutex);
        std::swap(tasks, self->m_taskQueue);
    }
    while (!tasks.empty()) {
        tasks.front().fn(self->m_base);
        tasks.pop();
    }

    // Redis 任务
    std::queue<RedisTaskEntry> redisTasks;
    {
        std::lock_guard<std::mutex> lock(self->m_redisTaskMutex);
        std::swap(redisTasks, self->m_redisTaskQueue);
    }
    while (!redisTasks.empty()) {
        if (self->m_redisCtx)
            redisTasks.front().fn(self->m_redisCtx);
        redisTasks.pop();
    }

    if (!self->m_running.load())
        event_base_loopbreak(self->m_base);
}

void EventWorker::onTimer(evutil_socket_t, short, void* arg)
{
    TimerEntry* entry = static_cast<TimerEntry*>(arg);
    if (entry && entry->fn) entry->fn();
}
