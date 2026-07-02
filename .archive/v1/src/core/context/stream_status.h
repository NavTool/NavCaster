#pragma once
#include "context_util.h"



// 数据流统计（速率计算）
class stream_status
{
private:
    struct Sample
    {
        int64_t time;
        double bytes;
    };

    std::deque<Sample> _recvHistory;
    std::deque<Sample> _sendHistory;
    int _windowSize = 60;

    std::string _uid;
    std::time_t _online_time = 0;
    std::time_t _update_time = 0;

    double _send_total = 0;
    double _send_speed = 0;
    double _recv_total = 0;
    double _recv_speed = 0;

    std::time_t _tcp_delay; // TCP延迟(微秒)

public:
    stream_status(std::string uid)
    {
        _uid = uid;
        _online_time = util_get_now_second();
        _update_time = util_get_now_second();
    }

    int add_recv(int size)
    {
        _recv_total += size;
        _update_time = util_get_now_second();
        _recvHistory.push_back({_update_time, _recv_total});
        cleanOld(_recvHistory, _update_time);
        _recv_speed = calcAvgSpeed(_recvHistory);
        return 0;
    }
    int add_send(int size)
    {
        _send_total += size;
        _update_time = util_get_now_second();
        _sendHistory.push_back({_update_time, _send_total});
        cleanOld(_sendHistory, _update_time);
        _send_speed = calcAvgSpeed(_sendHistory);
        return 0;
    }

    int add_delay(uint64_t delay)
    {
        _tcp_delay=delay;
        return 0;
    }

    double getSendTotal() const { return _send_total; }
    double getRecvTotal() const { return _recv_total; }

    int fromString(const std::string &str)
    {
        return 0; // 目前不需要从字符串解析状态，后续如果需要再实现
    } // 从proto转换为内部数据结构
    std::string toString()
    {
        // 刷新速度
        add_recv(0);
        add_send(0);

        // 创建一个proto
        caster::core::StreamState proto;

        proto.set_uid(_uid);
        proto.set_online_time(_online_time);
        proto.set_update_time(_update_time);

        proto.set_send_total(_send_total);
        proto.set_send_speed(_send_speed);
        proto.set_recv_total(_recv_total);
        proto.set_recv_speed(_recv_speed);

        return ProtoToJson(proto);
    }                 // 从内部数据结构转换为proto

private:
    void cleanOld(std::deque<Sample> &history, int64_t now)
    {
        while (!history.empty() && now - history.front().time > _windowSize)
        {
            history.pop_front();
        }
    }
    double calcAvgSpeed(const std::deque<Sample> &history) const
    {
        if (history.size() < 2)
            return 0.0;
        const Sample &first = history.front();
        const Sample &last = history.back();
        int64_t deltaTime = last.time - first.time;
        if (deltaTime <= 0)
            return 0.0;
        int64_t deltaBytes = last.bytes - first.bytes;
        return static_cast<double>(deltaBytes) / deltaTime;
    }
};







