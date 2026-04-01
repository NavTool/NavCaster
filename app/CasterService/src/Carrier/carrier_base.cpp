#include "carrier_base.h"
#include "knt.h"
#include "base64.h"

std::string build_nrtip_reply(ConnectType type, bool version2, bool chuncked)
{
    std::string str;
    if (type == CONNECT_TYPE_SERVER)
    {
        if (version2)
        {
            str += fmt::format("HTTP/1.1 200 OK\r\n");
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("Server: Ntrip {}_{}/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Date: {}\r\n", util_get_http_date());
            if (chuncked)
            {
                str += fmt::format("Transfer-Encoding: chunked\r\n");
            }
            str += fmt::format("Connection: close\r\n");
            str += fmt::format("\r\n");
        }
        else
        {
            str += fmt::format("ICY 200 OK\r\n");
            str += fmt::format("\r\n");
        }
    }
    else if (type == CONNECT_TYPE_CLIENT)
    {
        if (version2)
        {
            str += fmt::format("HTTP/1.1 200 OK\r\n");
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("Server: Ntrip {}_{}/2.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Date: {}\r\n", util_get_http_date());
            str += fmt::format("Cache-Control: no-store, no-cache, max-age=0\r\n");
            str += fmt::format("Pragma: no-cache\r\n");
            str += fmt::format("Connection: close\r\n");
            if (chuncked)
            {
                str += fmt::format("Transfer-Encoding: chunked\r\n");
            }
            str += fmt::format("Content-Type: gnss/data\r\n");
            str += fmt::format("\r\n");
        }
        else
        {
            str += fmt::format("ICY 200 OK\r\n");
            str += fmt::format("\r\n");
        }
    }

    return str;
}

std::string build_ntrip_request(ConnectType type, bool version2, std::string mpt, std::string host, std::string auth)
{
    std::string str;
    if (type == CONNECT_TYPE_PULL)
    {
        if (version2) // Ntrip/2.0
        {
            str += fmt::format("GET {} HTTP/1.1\r\n", mpt);
            str += fmt::format("Host: {}\r\n", host);
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("User-Agent: {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Authorization: Basic {}\r\n", auth);
            str += fmt::format("Connection: close\r\n");
            str += fmt::format("\r\n");
        }
        else // Ntrip/1.0
        {
            str += fmt::format("GET {} HTTP/1.0\r\n", mpt);
            str += fmt::format("User-Agent: {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Authorization: Basic {}\r\n", auth);
            str += fmt::format("\r\n");
        }
    }
    else if (type == CONNECT_TYPE_PUSH)
    {
        if (version2) // Ntrip/2.0
        {
            str += fmt::format("POST /{} HTTP/1.1\r\n", mpt);
            str += fmt::format("Host: {}\r\n", host);
            str += fmt::format("Ntrip-Version: Ntrip/2.0\r\n");
            str += fmt::format("User-Agent: {}/{}\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("Authorization: Basic {}\r\n", auth);
            str += fmt::format("Transfer-Encoding: chunked\r\n"); // 如果使用2.0, 默认使用chunked传输，但具体能不能开启，还要看服务端是否支持
            str += fmt::format("Connection: close\r\n");
            str += fmt::format("\r\n");
        }
        else // Ntrip/1.0
        {
            str += fmt::format("SOURCE {} /{} HTTP/1.0\r\n", auth, mpt);
            str += fmt::format("User-Agent: Ntrip {}_{}/1.0\r\n", PROJECT_SET_NAME, PROJECT_SET_VERSION);
            str += fmt::format("\r\n");
        }
    }

    return str;
}

bool verify_ntrip_response(const char *data, size_t len, bool &version2, bool &chuncked)
{

    return false;
}

carrier_base::carrier_base(ConnectInfo info)
{

    _info = info;
    _connect_key = info.connect_key();
    _mount_point = info.mount_point();
    _user_name = info.user_name();

    if (info.http_chunked() == "chunked")
    {
        _transfer_with_chunked = true;
    }
    if (info.ntrip_version() == "Ntrip/2.0")
    {
        _ntrip_version2 = true;
    }

    _send_evbuf = evbuffer_new();
    _recv_evbuf = evbuffer_new();
    _timeout_ev = event_new(connect_bev::getInstance()->get_base(), -1, EV_PERSIST, TimeoutCallback, this);

    _bev = connect_bev::getInstance()->get_bev(info.connect_key()); // 如果是还没有建立的连接，那么这个返回的是空指针
}

carrier_base::~carrier_base()
{
    connect_bev::getInstance()->del_bev(_info.connect_key());

    evbuffer_free(_send_evbuf);
    evbuffer_free(_recv_evbuf);

    event_free(_timeout_ev);
}

std::string carrier_base::create_bev(std::string addr, int port)
{
    return connect_bev::getInstance()->new_bev(addr, port);
}

int carrier_base::destory_bev(std::string connect_key)
{
    return connect_bev::getInstance()->del_bev(connect_key);
}

int carrier_base::start_bev(bool enable_read_cb, time_t read_timeout_sec, bool enable_write_cb, time_t write_timeout_sec)
{
    connect_bev::getInstance()->set_bev(_connect_key, enable_read_cb ? ReadCallback : nullptr, enable_write_cb ? WriteCallback : nullptr, EventCallback, this);
    connect_bev::getInstance()->set_timer(_connect_key, read_timeout_sec, write_timeout_sec);
    return 0;
}

int carrier_base::stop_bev()
{
    connect_bev::getInstance()->set_bev(_connect_key, nullptr, nullptr, nullptr, nullptr);
    connect_bev::getInstance()->del_timer(_connect_key);
    return 0;
}

int carrier_base::start_timeout_event(time_t timeout_sec)
{
    if (timeout_sec <= 0)
    {
        return 1; // 设置无效的超时时间，无法启动定时器事件
    }

    if (_timeout_ev_flag)
    {
        return 2; // 定时器事件已经启动，需要先停止才能启动
    }

    _timeout_tv.tv_sec = timeout_sec;
    _timeout_tv.tv_usec = 0;
    event_add(_timeout_ev, &_timeout_tv);
    _timeout_ev_flag = true;

    return 0;
}

int carrier_base::stop_timeout_event()
{
    if (!_timeout_ev_flag)
    {
        return 1; // 定时器事件未启动，无需停止
    }

    event_del(_timeout_ev);
    _timeout_ev_flag = false;

    return 0;
}

int carrier_base::auth_login(AuthType type)
{
    _auth_type = type;
    if (_auth_type == AuthType::UNKNOWN)
    {
        return 1; // 无效的AuthType
    }
    AUTH::Add_Login_Record(_user_name.c_str(), _connect_key.c_str(), AuthLoginCallback, this, type);
    return 0;
}

int carrier_base::auth_logout()
{
    if (_auth_type == AuthType::UNKNOWN)
    {
        return 1; // 无效的AuthType，无法执行登出操作
    }
    AUTH::Add_Logout_Record(_user_name.c_str(), _connect_key.c_str(), _auth_type);
    return 0;
}

int carrier_base::caster_register(CasterRegisterType type)
{
    _register_type = type;
    if (_register_type == CasterRegisterType::UNKNOWN)
    {
        return 1; // 无效的Type，无法执行
    }
    CASTER::Register_Record(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), CasterRegisterCallback, this, _register_type);
    return 0;
}

int carrier_base::caster_withdraw()
{
    if (_auth_type == AuthType::UNKNOWN)
    {
        return 1; // 无效的Type，无法执行
    }
    CASTER::Withdraw_Record(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), _register_type);
    return 0;
}

std::vector<uint8_t> carrier_base::read_data(bool chuncked)
{
    if (chuncked)
    {
        return read_data_from_chunk();
    }
    else
    {
        return read_data_from_evbuf();
    }
}

int carrier_base::send_data(const char *data, size_t len, bool chuncked)
{
    if (chuncked)
    {
        evbuffer_add_printf(_send_evbuf, "%lx\r\n", len);
        evbuffer_add(_send_evbuf, data, len);
    }

    evbuffer_add(_send_evbuf, data, len);
    bufferevent_write_buffer(_bev, _send_evbuf);

    return 0;
}

int carrier_base::publish_data(const char *data, size_t len)
{
    CASTER::Pub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), data, len, _register_type);
    return 0;
}

int carrier_base::subscribe()
{
    if (_register_type == CasterRegisterType::UNKNOWN)
    {
        return 1; // 无效的Type，无法执行
    }
    CASTER::Sub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), CasterSubscribeCallback, this, _register_type);
    return 0;
}

int carrier_base::subscribe(double lon, double lat)
{
    return 0;
}

int carrier_base::unsubscribe()
{
    if (_register_type == CasterRegisterType::UNKNOWN)
    {
        return 1; // 无效的Type，无法执行
    }
    CASTER::Unsub_Raw_Data(_connect_key.c_str(), _mount_point.c_str(), _user_name.c_str(), _register_type);
    return 0;
}

std::vector<uint8_t> carrier_base::read_data_from_evbuf()
{
    std::vector<uint8_t> datas;

    size_t length = evbuffer_get_length(_recv_evbuf);

    char *data = new char[length + 1];
    data[length] = '\0';
    evbuffer_remove(_recv_evbuf, data, length);

    datas.insert(datas.end(), data, data + length);
    delete[] data;

    return datas;
}

std::vector<uint8_t> carrier_base::read_data_from_chunk()
{
    std::vector<uint8_t> datas;
    if (_chunked_size == 0)
    {
        // 先读取一行
        char *chunk_head_data;
        size_t chunk_head_size;
        chunk_head_data = evbuffer_readln(_recv_evbuf, &chunk_head_size, EVBUFFER_EOL_CRLF);

        if (!chunk_head_data)
        {
            spdlog::warn("[{}:{}: chunked data error,close connect! {},{},{}", __class__, __func__, _info.mount_point(), _info.addr(), _info.port());
            stop();
            return datas;
        }
        sscanf(chunk_head_data, "%zx", &chunk_head_size);

        _chunked_size = chunk_head_size;
    }

    // 判断长剩余长度是否满足chunk长度（即块数据都已接收到）
    size_t length = evbuffer_get_length(_recv_evbuf);

    if (_chunked_size + 2 <= length) // 还有回车换行
    {
        char *data = new char[_chunked_size + 3];
        data[_chunked_size + 2] = '\0';

        evbuffer_remove(_recv_evbuf, data, _chunked_size);

        datas.insert(datas.end(), data, data + _chunked_size);

        _chunked_size = 0;
        delete[] data;
    }
    else
    {
        return datas;
        // 不满足，记录当前chunk长度，等待后续数据来了再发送
    }

    // 如果evbuffer中还有未发送的数据，那就再进行一次函数
    if (evbuffer_get_length(_recv_evbuf) > 0)
    {
        auto next_datas = read_data_from_chunk();
        datas.insert(datas.end(), next_datas.begin(), next_datas.end());
    }

    return datas;
}

void carrier_base::ReadCallback(bufferevent *bev, void *arg)
{
    auto svr = static_cast<carrier_base *>(arg);
    bufferevent_read_buffer(bev, svr->_recv_evbuf);
    svr->read_cb(bev);
}

void carrier_base::WriteCallback(bufferevent *bev, void *arg)
{
    auto svr = static_cast<carrier_base *>(arg);
    svr->write_cb(bev);
}

void carrier_base::EventCallback(bufferevent *bev, short events, void *arg)
{
    auto svr = static_cast<carrier_base *>(arg);

    spdlog::info("[{}:{}]: {}{}{}{}{}{}",
                 svr->__class__, __func__,
                 (events & BEV_EVENT_READING) ? "read" : "-",
                 (events & BEV_EVENT_WRITING) ? "write" : "-",
                 (events & BEV_EVENT_EOF) ? "eof" : "-",
                 (events & BEV_EVENT_ERROR) ? "error" : "-",
                 (events & BEV_EVENT_TIMEOUT) ? "timeout" : "-",
                 (events & BEV_EVENT_CONNECTED) ? "connected" : "-");

    svr->event_cb(bev, events);
}

void carrier_base::TimeoutCallback(evutil_socket_t fd, short events, void *arg)
{
    auto *svr = static_cast<carrier_base *>(arg);
    svr->timeout_cb();
}

void carrier_base::AuthLoginCallback(const char *request, void *arg, auth_reply *reply)
{
    auto *svr = static_cast<carrier_base *>(arg);
    svr->login_cb(reply);
}

void carrier_base::CasterRegisterCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *svr = static_cast<carrier_base *>(arg);
    svr->register_cb(reply);
}

void carrier_base::CasterSubscribeCallback(const char *request, void *arg, caster_reply *reply)
{
    auto *svr = static_cast<carrier_base *>(arg);
    svr->subscribe_cb(reply);
}
