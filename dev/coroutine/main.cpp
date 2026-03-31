// libevent_coroutine_connect.cpp
// Example: use C++20 coroutines + libevent/bufferevent to:
// 1) create a bufferevent and initiate TCP connect
// 2) await connection completion (co_await)
// 3) send authentication message
// 4) await response (co_await a line)
// 5) if auth OK -> periodically send data (co_await timers)
//    otherwise close the connection
//
// Build:
//   g++ -std=c++20 libevent_coroutine_connect.cpp -levent -pthread -O2 -o client
//
// Run (example):
//   ./client 127.0.0.1 9000
//
// Note: This is a minimal example focusing on coroutine glue — production code
// should add proper error handling, timeouts, and resource pooling.

#include <coroutine>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <sys/socket.h>
// #include <unistd.h>

#include <iostream>
#include <memory>
#include <string>
#include <optional>
#include <cstring>
#include <cstdlib>

// Simple EventLoop RAII
struct EventLoop {
    EventLoop() { base = event_base_new(); }
    ~EventLoop() { if (base) event_base_free(base); }
    event_base* get() const { return base; }
    void run() { if (base) event_base_dispatch(base); }
private:
    event_base* base = nullptr;
};

// DetachedTask: fire-and-forget coroutine type
struct DetachedTask {
    struct promise_type {

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

// TimerAwaitable: resumes after timeout using libevent timer
struct TimerAwaitable {
    event_base* base;
    timeval tv;
    TimerAwaitable(event_base* b, int sec, int usec = 0) : base(b) {
        tv.tv_sec = sec; tv.tv_usec = usec;
    }

    // 这个函数用来定义当调用这个函数的时候，是否需要挂起当前的协程。对于这个例子来说，我们总是需要等待，所以返回 false
    // 如果返回 true，表示不需要挂起，协程会继续执行下去；如果返回 false，表示需要挂起，协程会暂停执行，直到满足某个条件（比如定时器到期）后再恢复执行。
    bool await_ready() const noexcept { return false; }

    // 这个函数用来定义当协程挂起时，应该执行什么操作。对于这个例子来说，我们需要设置一个 libevent 定时器，当定时器到期时恢复协程的执行。
    void await_suspend(std::coroutine_handle<> h) {
        struct State {
            std::coroutine_handle<> handle;
            struct event* ev;
            timeval tv;
        };

        State* s = new State{h, nullptr, tv};

        s->ev = event_new(base, -1, 0,
            [](evutil_socket_t, short, void* arg) {
                State* st = static_cast<State*>(arg);
                if (st->ev) event_free(st->ev);
                auto hdl = st->handle;
                delete st;
                hdl.resume(); // 恢复协程的执行
            }, s);

        event_add(s->ev, &s->tv);
    }

    // 这个函数用来定义当协程恢复执行时，应该返回什么结果。对于这个例子来说，我们不需要返回任何结果，所以返回 void。
    void await_resume() const noexcept {}
};

// Forward declaration
struct Connection;

// ReadLineAwaitable: await until a '\n'-terminated line is available or connection closed.
struct ReadLineAwaitable {
    std::shared_ptr<Connection> conn;

    ReadLineAwaitable(std::shared_ptr<Connection> c) : conn(std::move(c)) {}

    // 当调用这个函数的时候 检查Bufferevent的函数是否已经有一行数据可读，如果有则返回true，表示协程可以继续执行；如果没有，则返回false，表示协程需要挂起，等待数据到来。
    bool await_ready() const noexcept;
    // 线程挂起，将这个句柄传递给连接对象
    void await_suspend(std::coroutine_handle<> h) noexcept;
    std::optional<std::string> await_resume() noexcept;
};

// ConnectAwaitable: await until connection is established or fails.
struct ConnectAwaitable {
    std::shared_ptr<Connection> conn;
    const sockaddr* addr;
    socklen_t addrlen;

    ConnectAwaitable(std::shared_ptr<Connection> c, const sockaddr* a, socklen_t al)
        : conn(std::move(c)), addr(a), addrlen(al) {}

    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> h) noexcept;
    bool await_resume() noexcept;
};

// Coroutine to continuously read and print incoming lines until connection closes
DetachedTask print_incoming(std::shared_ptr<Connection> conn) {
    while (conn) {
        auto line = co_await ReadLineAwaitable(conn);
        if (!line) break;
        std::cout << "recv: " << *line << std::endl;
    }
    co_return;
}

// Connection: owns bufferevent and stores one-shot waiter handles
struct Connection : std::enable_shared_from_this<Connection> {
    event_base* base = nullptr;
    struct bufferevent* bev = nullptr;

    // one-shot waiters (at most one waiter of each kind at a time in this simple example)
    std::coroutine_handle<> connect_waiter{};
    std::coroutine_handle<> read_waiter{};

    bool connected = false;
    bool closed = false;

    Connection(event_base* b) : base(b) {
        bev = bufferevent_socket_new(base, -1,
            BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
        // set callbacks with this as context
        bufferevent_setcb(bev, &Connection::libevent_read_cb, nullptr, &Connection::libevent_event_cb, this);
        // enable read notifications
        bufferevent_enable(bev, EV_READ | EV_WRITE);
    }

    ~Connection() {
        if (bev) {
            bufferevent_free(bev);
            bev = nullptr;
        }
    }

    void send(const std::string& s) {
        if (bev) bufferevent_write(bev, s.data(), s.size());
    }

    static void libevent_read_cb(struct bufferevent* bev, void* ctx) {
        Connection* self = static_cast<Connection*>(ctx);
        // If a read waiter is waiting and there's a full line, resume it.
        if (self->read_waiter) {
            // Check if there's a newline available; if not, still resume (we rely on await_ready logic)
            self->read_waiter.resume();
            self->read_waiter = {};
        }
    }

    static void libevent_event_cb(struct bufferevent* bev, short events, void* ctx) {
        Connection* self = static_cast<Connection*>(ctx);
        if (events & BEV_EVENT_CONNECTED) {
            self->connected = true;
            if (self->connect_waiter) {
                self->connect_waiter.resume();
                self->connect_waiter = {};
            }
        } else if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
            self->closed = true;
            // resume any pending waiters so they can react to closure
            if (self->connect_waiter) {
                self->connect_waiter.resume();
                self->connect_waiter = {};
            }
            if (self->read_waiter) {
                self->read_waiter.resume();
                self->read_waiter = {};
            }
        }
    }
};

// Implementation of ReadLineAwaitable
bool ReadLineAwaitable::await_ready() const noexcept {
    if (!conn || conn->closed || !conn->bev) return true; // resume immediately and return nullopt
    struct evbuffer* in = bufferevent_get_input(conn->bev);
    size_t len = evbuffer_get_length(in);
    if (len == 0) return false;
    // Make first len bytes contiguous and search for '\n'
    unsigned char* data = evbuffer_pullup(in, static_cast<int>(len));
    if (!data) return false;
    for (size_t i = 0; i < len; ++i) if (data[i] == '\n') return true;
    return false;
}

void ReadLineAwaitable::await_suspend(std::coroutine_handle<> h) noexcept {
    if (!conn) return;
    // store the waiter; bufferevent read callback will resume it when data arrives
    conn->read_waiter = h;
}

std::optional<std::string> ReadLineAwaitable::await_resume() noexcept {
    if (!conn || conn->closed || !conn->bev) return std::nullopt;
    struct evbuffer* in = bufferevent_get_input(conn->bev);
    size_t n = 0;
    char* line = evbuffer_readln(in, &n, EVBUFFER_EOL_CRLF);
    if (!line) {
        // No complete line available (could happen if connection closed); return nullopt
        return std::nullopt;
    }
    std::string s(line, n);
    free(line);
    return s;
}

// Implementation of ConnectAwaitable
bool ConnectAwaitable::await_ready() const noexcept {
    return conn && conn->connected;
}

void ConnectAwaitable::await_suspend(std::coroutine_handle<> h) noexcept {
    if (!conn || conn->closed || !conn->bev) {
        // nothing we can do, resume immediately
        h.resume();
        return;
    }
    conn->connect_waiter = h;
    // initiate connect (non-blocking). If this call fails immediately, resume and mark closed.
    int rc = bufferevent_socket_connect(conn->bev, addr, addrlen);
    if (rc < 0) {
        // immediate error
        conn->closed = true;
        if (conn->connect_waiter) {
            auto w = conn->connect_waiter;
            conn->connect_waiter = {};
            w.resume();
        }
    }
}

bool ConnectAwaitable::await_resume() noexcept {
    return conn && conn->connected && !conn->closed;
}

// Example client coroutine: does the full flow described by the user
DetachedTask client_flow(event_base* base, const char* ip, uint16_t port) {
    // create connection object (shared_ptr for safe lifetime across callbacks)
    auto conn = std::make_shared<Connection>(base);

    // prepare sockaddr_in
    sockaddr_in sin;
    std::memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sin.sin_addr) != 1) {
        std::cerr << "Invalid IP address\n";
        co_return;
    }

    std::cout << "connecting...\n";
    bool ok = co_await ConnectAwaitable(conn, reinterpret_cast<const sockaddr*>(&sin), sizeof(sin));
    if (!ok) {
        std::cerr << "connect failed or closed\n";
        co_return;
    }
    std::cout << "connected\n";

    // send authentication
    std::string auth = "AUTH secret\n";
    conn->send(auth);
    std::cout << "sent auth\n";

    // wait for a single-line reply
    auto reply = co_await ReadLineAwaitable(conn);
    if (!reply) {
        std::cerr << "connection closed before auth reply\n";
        co_return;
    }
    std::cout << "auth reply: " << *reply << "\n";

    // simple check: expect "OK"
    if (*reply == "OK") {
        std::cout << "auth ok, starting periodic sends\n";
        // start a coroutine to print incoming lines concurrently
        print_incoming(conn);

        // send periodically until connection closes
        while (!conn->closed) {
            conn->send("DATA hello\n");
            // use timer awaitable to wait e.g. 5 seconds between sends
            co_await TimerAwaitable(base, 5, 0);
        }
        std::cout << "connection closed, stopping periodic sends\n";
        co_return;
    } else {
        std::cout << "auth failed, closing connection\n";
        // close bufferevent (Connection destructor will free if still present)
        if (conn->bev) {
            bufferevent_free(conn->bev);
            conn->bev = nullptr;
        }
        conn->closed = true;
        co_return;
    }
}

int main(int argc, char** argv) {
    // if (argc < 3) {
    //     std::cerr << "usage: " << argv[0] << " <server-ip> <port>\n";
    //     return 1;
    // }
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        // spdlog::info("WSAStartup failed! exit.");
        return 1;
    }
#endif

    const char* ip = "127.0.0.1";
    uint16_t port = static_cast<uint16_t>(std::stoi("2101"));

    EventLoop loop;
    // start the coroutine client flow (fire-and-forget)
    client_flow(loop.get(), ip, port);

    // run the libevent loop (this will drive the coroutine resumes)
    loop.run();

#ifdef WIN32
    WSACleanup();
#endif


    return 0;
}