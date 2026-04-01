#pragma once

#include <string>
#include <deque>
#include <ctime>

#include "proto/src/core/StreamState.pb.h"


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

public:
    stream_status(std::string uid); 

    int add_recv(int size);
    int add_send(int size);

    int fromString(const std::string &str); // 从proto转换为内部数据结构
    std::string toString();                 // 从内部数据结构转换为proto

private:
    void cleanOld(std::deque<Sample> &history, int64_t now);
    double calcAvgSpeed(const std::deque<Sample> &history) const;
};
